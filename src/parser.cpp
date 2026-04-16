#include "parser.h"

#include "tokenizer.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rs {

// ---------------------------------------------------------------------------
// Import extraction
//
// We handle two Python import forms without pulling in a full parser:
//   import foo.bar.baz        → "foo.bar.baz"  (full dotted path)
//   from foo.bar import baz   → "foo.bar"      (full source module)
//   from .utils import x      → ".utils"       (relative, single dot)
//   from ..core import y      → "..core"       (relative, multi-dot)
//   from . import z           → "."            (package-relative)
//
// The parser preserves full module paths and relative import syntax.
// Resolution to file IDs happens in the graph builder, not here.
// ---------------------------------------------------------------------------

static void extract_imports(std::string_view content, std::vector<std::string>& out) {
    const char* p = content.data();
    const char* end = p + content.size();

    auto skip_spaces = [](const char* s, const char* e) {
        while (s < e && (*s == ' ' || *s == '\t'))
            ++s;
        return s;
    };

    auto read_identifier = [](const char* s, const char* e, std::string& out_id) {
        // Read a Python dotted identifier (foo.bar.baz) or a relative import
        // prefix (.foo, ..bar.baz) and return ptr past it.
        out_id.clear();
        while (s < e && (std::isalnum(static_cast<unsigned char>(*s)) || *s == '_' || *s == '.')) {
            out_id += *s++;
        }
        return s;
    };

    while (p < end) {
        const char* line_end = p;
        while (line_end < end && *line_end != '\n')
            ++line_end;

        const char* lp = skip_spaces(p, line_end);

        // Match: import <module> [as <alias>] [, <module2> ...]
        if (line_end - lp >= 7 && lp[0] == 'i' && lp[1] == 'm' && lp[2] == 'p' && lp[3] == 'o' &&
            lp[4] == 'r' && lp[5] == 't' && (lp[6] == ' ' || lp[6] == '\t')) {

            lp += 7;
            while (lp < line_end) {
                lp = skip_spaces(lp, line_end);
                std::string mod;
                lp = read_identifier(lp, line_end, mod);
                if (!mod.empty()) {
                    out.push_back(mod);
                }
                // skip optional "as alias"
                lp = skip_spaces(lp, line_end);
                if (line_end - lp >= 3 && lp[0] == 'a' && lp[1] == 's' &&
                    (lp[2] == ' ' || lp[2] == '\t')) {
                    lp += 3;
                    std::string alias;
                    lp = read_identifier(lp, line_end, alias);
                }
                lp = skip_spaces(lp, line_end);
                if (lp < line_end && *lp == ',')
                    ++lp;
                else
                    break;
            }

        }
        // Match: from <module> import ...
        // Includes relative imports: from .foo import x, from ..bar import y
        else if (line_end - lp >= 5 && lp[0] == 'f' && lp[1] == 'r' && lp[2] == 'o' &&
                 lp[3] == 'm' && (lp[4] == ' ' || lp[4] == '\t')) {

            lp += 5;
            lp = skip_spaces(lp, line_end);
            std::string mod;
            lp = read_identifier(lp, line_end, mod);
            if (!mod.empty()) {
                out.push_back(mod);
            }
        }

        p = line_end + 1;
    }
}

// ---------------------------------------------------------------------------
// JS/TS import extraction
//
// A state machine walks the source, tracking whether the next string literal
// is a module specifier.  Comments, strings, and dotted property accesses are
// all skipped without triggering capture.
//
// Patterns handled:
//   import "mod";
//   import x from "mod";
//   import { a, b } from "mod";
//   import * as ns from "mod";
//   import x, { y } from "mod";
//   import type { T } from "mod";
//   export * from "mod";
//   export { a } from "mod";
//   const x = require("mod");
//   import("mod");                  // dynamic
//
// Non-captures (false-positive leaks fixed here):
//   obj.from("x")        // `from` after dot is a method
//   from("x")            // bare `from` outside import/export context
//   foo.require("x")     // `require` after dot is a method
//   import.meta.glob(…)  // `.` cancels import context
//   import x from \n const s = "hello"  // strict ExpectString cancels
//
// Relative specifiers (starting with "." or "..") are kept verbatim; the
// graph builder resolves them.  Bare/npm specifiers are dropped there, not
// here.  Template-literal interpolation (${…}) is not interpreted — whole
// template strings are opaque, which is fine because static imports cannot
// appear inside one.
// ---------------------------------------------------------------------------
static void extract_js_imports(std::string_view content, std::vector<std::string>& out) {
    // `prev_was_dot` suppresses keyword recognition for property accesses:
    // after `.`, the next identifier is always a member name, never `from`,
    // `import`, or `require`.  ExpectString is strict — any non-string
    // significant token cancels it, so stray strings later in the file are
    // not misattributed.
    enum class State {
        None,
        ImportHead,   // just saw bare `import`
        ImportClause, // inside an import clause, awaiting `from`
        ExportHead,   // just saw `export`, may be re-export awaiting `from`
        SawRequire,   // just saw bare `require`, awaiting `(`
        ExpectString, // next string literal is a module specifier
    };
    State state = State::None;
    bool prev_was_dot = false;

    auto is_ident_start = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
    };
    auto is_ident_char = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '$';
    };

    const char* p = content.data();
    const char* const end = p + content.size();

    while (p < end) {
        const unsigned char c = static_cast<unsigned char>(*p);

        // Whitespace — preserves state and prev_was_dot.
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++p;
            continue;
        }

        // Line comment — preserves state and prev_was_dot.
        if (c == '/' && p + 1 < end && p[1] == '/') {
            p += 2;
            while (p < end && *p != '\n')
                ++p;
            continue;
        }

        // Block comment — preserves state and prev_was_dot.
        if (c == '/' && p + 1 < end && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                ++p;
            if (p + 1 < end)
                p += 2;
            else
                p = end;
            continue;
        }

        // String literal — double, single, or backtick.
        if (c == '"' || c == '\'' || c == '`') {
            const char quote = static_cast<char>(c);
            ++p;
            const char* const str_start = p;
            while (p < end && *p != quote) {
                if (*p == '\\' && p + 1 < end)
                    p += 2;
                else
                    ++p;
            }
            const char* const str_end = p;
            if (p < end)
                ++p;

            // Skip template literals as module specifiers. Dynamic imports
            // like import(`./${name}`) are not statically resolvable.
            if ((state == State::ImportHead || state == State::ExpectString) && quote != '`') {
                out.emplace_back(str_start, static_cast<std::size_t>(str_end - str_start));
            }
            state = State::None;
            prev_was_dot = false;
            continue;
        }

        // Open paren — completes dynamic import or require call.
        if (c == '(') {
            if (state == State::ImportHead || state == State::SawRequire)
                state = State::ExpectString;
            else
                state = State::None;
            ++p;
            prev_was_dot = false;
            continue;
        }

        // Dot — cancels import context (e.g. `import.meta`) and marks the
        // next identifier as a property access.
        if (c == '.') {
            state = State::None;
            prev_was_dot = true;
            ++p;
            continue;
        }

        // `=` or `;` end any pending import/require context.
        if (c == '=' || c == ';') {
            state = State::None;
            prev_was_dot = false;
            ++p;
            continue;
        }

        // Identifier / keyword.
        if (is_ident_start(c)) {
            const char* const word_start = p;
            while (p < end && is_ident_char(static_cast<unsigned char>(*p)))
                ++p;
            const std::string_view w(word_start, static_cast<std::size_t>(p - word_start));
            const bool after_dot = prev_was_dot;
            prev_was_dot = false;

            // ExpectString is strict: any identifier here means the expected
            // module-specifier string never arrived — cancel.
            if (state == State::ExpectString) {
                state = State::None;
                continue;
            }

            // After a dot this is a property name, never a keyword.
            if (after_dot)
                continue;

            if (w == "import") {
                state = State::ImportHead;
            } else if (w == "export") {
                state = State::ExportHead;
            } else if (w == "from") {
                if (state == State::ImportHead || state == State::ImportClause ||
                    state == State::ExportHead)
                    state = State::ExpectString;
                else
                    state = State::None;
            } else if (w == "require") {
                state = State::SawRequire;
            } else {
                // Generic identifier: ImportHead → ImportClause (clause body
                // has begun, now waiting for `from`); SawRequire cancels (the
                // next token wasn't `(`); other states are preserved.
                if (state == State::ImportHead)
                    state = State::ImportClause;
                else if (state == State::SawRequire)
                    state = State::None;
            }
            continue;
        }

        // Any other significant character: cancel strict contexts, transition
        // ImportHead → ImportClause (e.g. on `{` or `*`), otherwise preserve.
        if (state == State::ExpectString || state == State::SawRequire)
            state = State::None;
        else if (state == State::ImportHead)
            state = State::ImportClause;
        prev_was_dot = false;
        ++p;
    }
}

// Deduplicate `v` in place, preserving the first-occurrence order.
static void dedupe_preserve_order(std::vector<std::string>& v) {
    if (v.size() < 2)
        return;
    std::unordered_set<std::string> seen;
    std::vector<std::string> out;
    seen.reserve(v.size());
    out.reserve(v.size());
    for (auto& s : v) {
        if (seen.insert(s).second) {
            out.push_back(std::move(s));
        }
    }
    v = std::move(out);
}

// ---------------------------------------------------------------------------
// File reading
// ---------------------------------------------------------------------------

// Read entire file into a std::string.  Returns false on failure.
static bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    if (size <= 0) {
        out.clear();
        return true; // empty file is valid
    }
    out.resize(static_cast<std::size_t>(size));
    f.seekg(0);
    f.read(out.data(), size);
    return f.good() || f.eof();
}

static bool is_python_extension(std::string_view ext) {
    return ext == ".py" || ext == ".pyi";
}

static bool is_js_like_extension(std::string_view ext) {
    return ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx" || ext == ".mjs" ||
           ext == ".cjs";
}

static bool is_supported_source_extension(std::string_view ext) {
    return is_python_extension(ext) || is_js_like_extension(ext);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::optional<ParsedFile> parse_file(const std::filesystem::path& path) {
    ParsedFile pf;
    pf.path = path.string();

    if (!read_file(path, pf.content))
        return std::nullopt;

    // Extract imports BEFORE tokenizing: the tokenizer lowercases content
    // in place, which would corrupt case-sensitive JS/TS module specifiers.
    const std::string ext = path.extension().string();
    if (is_python_extension(ext)) {
        extract_imports(pf.content, pf.imports);
    } else if (is_js_like_extension(ext)) {
        extract_js_imports(pf.content, pf.imports);
    }
    dedupe_preserve_order(pf.imports);

    // Tokenize (mutates content to lowercase)
    pf.token_views = tokenize(pf.content);

    return pf;
}

std::vector<ParsedFile> parse_directory(const std::filesystem::path& root) {
    std::vector<ParsedFile> results;

    std::error_code ec;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_regular_file())
            continue;
        const std::string ext = entry.path().extension().string();
        if (!is_supported_source_extension(ext))
            continue;

        auto pf = parse_file(entry.path());
        if (pf)
            results.push_back(std::move(*pf));
    }

    return results;
}

} // namespace rs
