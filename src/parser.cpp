#include "parser.h"
#include "tokenizer.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace rs {

// ---------------------------------------------------------------------------
// Import extraction
//
// We handle two Python import forms without pulling in a full parser:
//   import foo.bar
//   from foo.bar import baz [as qux] [, ...]
//
// We extract the top-level module name (everything before the first dot).
// This is intentionally simple and fast — correctness matters more than
// handling every edge case (dynamic imports, __import__, etc.).
// ---------------------------------------------------------------------------

static void extract_imports(std::string_view content,
                            std::vector<std::string>& out) {
    // We scan line-by-line with a single pass — no regex overhead in the hot
    // path.  Each line we test for the two import patterns manually.
    const char* p   = content.data();
    const char* end = p + content.size();

    auto skip_spaces = [](const char* s, const char* e) {
        while (s < e && (*s == ' ' || *s == '\t')) ++s;
        return s;
    };

    auto read_identifier = [](const char* s, const char* e, std::string& out_id) {
        // Read a Python dotted identifier (foo.bar.baz) and return ptr past it.
        out_id.clear();
        while (s < e && (std::isalnum(static_cast<unsigned char>(*s))
                          || *s == '_' || *s == '.')) {
            out_id += *s++;
        }
        return s;
    };

    auto top_level = [](const std::string& module) -> std::string {
        // "foo.bar.baz" → "foo"
        auto dot = module.find('.');
        return (dot == std::string::npos) ? module : module.substr(0, dot);
    };

    while (p < end) {
        // Find end of line
        const char* line_end = p;
        while (line_end < end && *line_end != '\n') ++line_end;

        const char* lp = skip_spaces(p, line_end);

        // Match: import <module> [as <alias>] [, <module2> ...]
        if (line_end - lp >= 7 &&
            lp[0]=='i' && lp[1]=='m' && lp[2]=='p' && lp[3]=='o' &&
            lp[4]=='r' && lp[5]=='t' &&
            (lp[6] == ' ' || lp[6] == '\t')) {

            lp += 7;
            // There can be multiple comma-separated modules
            while (lp < line_end) {
                lp = skip_spaces(lp, line_end);
                std::string mod;
                lp = read_identifier(lp, line_end, mod);
                if (!mod.empty()) {
                    out.push_back(top_level(mod));
                }
                // skip optional "as alias"
                lp = skip_spaces(lp, line_end);
                if (line_end - lp >= 3 &&
                    lp[0]=='a' && lp[1]=='s' && (lp[2]==' '||lp[2]=='\t')) {
                    lp += 3;
                    std::string alias;
                    lp = read_identifier(lp, line_end, alias);
                }
                lp = skip_spaces(lp, line_end);
                if (lp < line_end && *lp == ',') ++lp;
                else break;
            }

        }
        // Match: from <module> import ...
        else if (line_end - lp >= 5 &&
                 lp[0]=='f' && lp[1]=='r' && lp[2]=='o' && lp[3]=='m' &&
                 (lp[4] == ' ' || lp[4] == '\t')) {

            lp += 5;
            lp = skip_spaces(lp, line_end);
            std::string mod;
            lp = read_identifier(lp, line_end, mod);
            if (!mod.empty() && mod != ".") { // skip relative-only imports for now
                out.push_back(top_level(mod));
            }
            // We don't need what's after "import" for the module name
        }

        p = line_end + 1; // advance past '\n'
    }
}

// ---------------------------------------------------------------------------
// File reading
// ---------------------------------------------------------------------------

// Read entire file into a std::string.  Returns false on failure.
static bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::optional<ParsedFile> parse_file(const std::filesystem::path& path) {
    ParsedFile pf;
    pf.path = path.string();

    if (!read_file(path, pf.content)) return std::nullopt;

    // Extract imports BEFORE tokenizing (tokenizer lowercases content in-place)
    extract_imports(pf.content, pf.imports);

    // Deduplicate imports (preserves first occurrence order)
    {
        std::vector<std::string> seen;
        seen.reserve(pf.imports.size());
        std::vector<std::string> deduped;
        for (auto& imp : pf.imports) {
            bool found = false;
            for (auto& s : seen) { if (s == imp) { found = true; break; } }
            if (!found) { seen.push_back(imp); deduped.push_back(std::move(imp)); }
        }
        pf.imports = std::move(deduped);
    }

    // Tokenize (mutates content to lowercase)
    pf.token_views = tokenize(pf.content);

    return pf;
}

std::vector<ParsedFile> parse_directory(const std::filesystem::path& root) {
    std::vector<ParsedFile> results;

    std::error_code ec;
    for (auto const& entry :
         std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".py") continue;

        auto pf = parse_file(entry.path());
        if (pf) results.push_back(std::move(*pf));
    }

    return results;
}

} // namespace rs
