#include "parser.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                  \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        if ((a) != (b)) {                                                                          \
            std::fprintf(stderr, "FAIL  %s:%d  (%s) == (%s)\n", __FILE__, __LINE__, #a, #b);       \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)
// NOLINTEND(cppcoreguidelines-macro-usage)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static fs::path g_tmp_root;

static void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
}

// Write content to a temp .py file, parse it, and return its imports.
// Reuses the same scratch path so each test starts clean.
static std::vector<std::string> imports_from(const std::string& content) {
    auto p = g_tmp_root / "scratch.py";
    write_file(p, content);
    auto pf = rs::parse_file(p);
    if (!pf)
        return {};
    return pf->imports;
}

// Same but for JS/TS: writes a .ts file so the JS/TS extraction path runs.
static std::vector<std::string> ts_imports_from(const std::string& content) {
    auto p = g_tmp_root / "scratch.ts";
    write_file(p, content);
    auto pf = rs::parse_file(p);
    if (!pf)
        return {};
    return pf->imports;
}

// ---------------------------------------------------------------------------
// Import extraction — "import" form
// ---------------------------------------------------------------------------

static void test_import_single() {
    auto imp = imports_from("import os\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "os");
}

static void test_import_multi_comma_with_alias() {
    // "import a, b as c" → ["a", "b"]; the alias "c" must not appear
    auto imp = imports_from("import a, b as c\n");
    CHECK_EQ(imp.size(), 2u);
    CHECK(imp[0] == "a");
    CHECK(imp[1] == "b");
}

static void test_import_leading_spaces() {
    // Indented imports (e.g. inside a function body) are still extracted
    auto imp = imports_from("    import os\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "os");
}

static void test_import_leading_tab() {
    auto imp = imports_from("\timport sys\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "sys");
}

// ---------------------------------------------------------------------------
// Import extraction — "from … import" form
// ---------------------------------------------------------------------------

static void test_from_dotted_import() {
    // Full dotted module path is preserved: "from x.y import z" → ["x.y"]
    auto imp = imports_from("from x.y import z\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "x.y");
}

static void test_from_relative_single_dot() {
    // "from . import local" — relative import, recorded as "."
    auto imp = imports_from("from . import local\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == ".");
}

static void test_from_relative_double_dot() {
    // "from ..pkg import mod" → ["..pkg"]
    auto imp = imports_from("from ..pkg import mod\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "..pkg");
}

static void test_from_relative_single_dot_name() {
    // "from .utils import helper" → [".utils"]
    auto imp = imports_from("from .utils import helper\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == ".utils");
}

static void test_from_relative_triple_dot() {
    // "from ...deep.mod import x" → ["...deep.mod"]
    auto imp = imports_from("from ...deep.mod import x\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "...deep.mod");
}

static void test_import_dotted_preserves_full_path() {
    // "import pkg.sub.mod" → ["pkg.sub.mod"]
    auto imp = imports_from("import pkg.sub.mod\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "pkg.sub.mod");
}

static void test_import_multi_dotted_with_alias() {
    // "import foo.bar, baz.qux as q" → ["foo.bar", "baz.qux"]
    auto imp = imports_from("import foo.bar, baz.qux as q\n");
    CHECK_EQ(imp.size(), 2u);
    CHECK(imp[0] == "foo.bar");
    CHECK(imp[1] == "baz.qux");
}

// ---------------------------------------------------------------------------
// Import extraction — whitespace and comment handling
// ---------------------------------------------------------------------------

static void test_inline_comment_ignored() {
    // Trailing " # …" must not corrupt the module name
    auto imp = imports_from("import os  # system module\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "os");
}

static void test_empty_lines_ignored() {
    // Blank lines around a real import must not produce extra entries
    auto imp = imports_from("\n\nimport os\n\n");
    CHECK_EQ(imp.size(), 1u);
    CHECK(imp[0] == "os");
}

static void test_comment_line_ignored() {
    // A line that is entirely a comment must not be parsed as an import
    auto imp = imports_from("# import os\n");
    CHECK(imp.empty());
}

// ---------------------------------------------------------------------------
// Deduplication
// ---------------------------------------------------------------------------

static void test_duplicate_imports_deduped() {
    // The same module appearing on multiple lines must be stored only once.
    // Insertion order of unique entries is preserved.
    auto imp = imports_from("import os\nimport os\nimport sys\n");
    CHECK_EQ(imp.size(), 2u);
    if (imp.size() == 2u) {
        CHECK(imp[0] == "os");
        CHECK(imp[1] == "sys");
    }
}

// ---------------------------------------------------------------------------
// JS/TS import extraction
//
// The parser preserves module specifiers verbatim — including relative
// specifiers like "./foo" and "../bar".  Resolution to file_ids (and the
// dropping of bare/npm specifiers) happens later in the graph builder.
// ---------------------------------------------------------------------------

static void test_js_default_import() {
    auto imp = ts_imports_from("import x from \"mod\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_named_import() {
    auto imp = ts_imports_from("import { a, b } from \"mod\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_namespace_import() {
    auto imp = ts_imports_from("import * as ns from \"mod\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_mixed_default_and_named() {
    // `import x, { y } from "mod"` — one module, captured once
    auto imp = ts_imports_from("import x, { y } from \"mod\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_bare_side_effect_import() {
    // `import "./styles.css"` — no `from`, still an import
    auto imp = ts_imports_from("import \"./styles.css\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "./styles.css");
}

static void test_js_type_only_import() {
    auto imp = ts_imports_from("import type { Foo } from \"foo-lib\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "foo-lib");
}

static void test_js_export_star_from() {
    auto imp = ts_imports_from("export * from \"mod\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_export_named_from() {
    auto imp = ts_imports_from("export { a, b } from \"mod\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_require() {
    auto imp = ts_imports_from("const fs = require(\"fs\");\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "fs");
}

static void test_js_dynamic_import() {
    auto imp = ts_imports_from("async function f(){ const m = await import(\"./mod\"); }\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "./mod");
}

static void test_js_dynamic_import_template_literal_not_captured() {
    auto imp = ts_imports_from("async function f(){ return import(`./${name}`); }\n");
    CHECK(imp.empty());
}

static void test_js_multiline_import() {
    // Named import with the clause and `from` split across several lines
    auto imp = ts_imports_from("import {\n  a,\n  b,\n  c,\n} from \"mod\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_relative_specifier_kept_verbatim() {
    // Relative paths are kept as-is so the graph builder can resolve them
    auto imp = ts_imports_from("import { Button } from \"../components/Button\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "../components/Button");
}

static void test_js_line_comment_ignored() {
    // `// import "nope"` must not be parsed as an import
    auto imp = ts_imports_from("// import \"nope\"\nimport x from \"real\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "real");
}

static void test_js_block_comment_ignored() {
    auto imp = ts_imports_from("/* import \"nope\"; */\nimport x from \"real\";\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "real");
}

static void test_js_string_literal_false_positive_ignored() {
    // A quoted string inside normal code (not an import) must not be captured
    auto imp = ts_imports_from("console.log(\"import x from 'nope'\");\n");
    CHECK(imp.empty());
}

static void test_js_require_identifier_not_called() {
    // `requireLogin` is a plain identifier, not the `require` keyword
    auto imp = ts_imports_from("const requireLogin = true;\n");
    CHECK(imp.empty());
}

static void test_js_multiple_imports_and_dedup() {
    // Many imports in one file; one duplicate that must collapse to a single entry
    const char* src = "import a from \"alpha\";\n"
                      "import b from \"beta\";\n"
                      "import a2 from \"alpha\";\n" // duplicate module, different local name
                      "const g = require(\"gamma\");\n";
    auto imp = ts_imports_from(src);
    CHECK_EQ(imp.size(), 3u);
    if (imp.size() == 3u) {
        CHECK(imp[0] == "alpha");
        CHECK(imp[1] == "beta");
        CHECK(imp[2] == "gamma");
    }
}

static void test_js_extension_variants_parse_as_js() {
    // Every JS-family extension must go through JS/TS extraction, not Python
    const std::string src = "import x from \"mod\";\n";
    for (const char* ext : {".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx"}) {
        auto p = g_tmp_root / (std::string("variant") + ext);
        write_file(p, src);
        auto pf = rs::parse_file(p);
        CHECK(pf.has_value());
        if (pf) {
            CHECK_EQ(pf->imports.size(), 1u);
            if (!pf->imports.empty())
                CHECK(pf->imports[0] == "mod");
        }
    }
}

// ---------------------------------------------------------------------------
// JS/TS hardening — false-positive regressions
//
// These exercises nail down the state-machine transitions so that
// `obj.from("x")`, bare `from("x")`, `foo.require("x")`, `import.meta.glob(…)`
// and similar non-import syntax do not leak into the extracted import list.
// ---------------------------------------------------------------------------

static void test_js_from_method_call_not_captured() {
    auto imp = ts_imports_from("const a = obj.from(\"not-a-module\");\n");
    CHECK(imp.empty());
}

static void test_js_bare_from_call_not_captured() {
    auto imp = ts_imports_from("const a = from(\"not-a-module\");\n");
    CHECK(imp.empty());
}

static void test_js_array_from_not_captured() {
    auto imp = ts_imports_from("const xs = Array.from([1, 2, 3]);\n");
    CHECK(imp.empty());
}

static void test_js_method_require_not_captured() {
    auto imp = ts_imports_from("const m = foo.require(\"not-a-module\");\n");
    CHECK(imp.empty());
}

static void test_js_require_bracket_access_not_captured() {
    // `require["x"]` is a property access, not a call
    auto imp = ts_imports_from("const r = require[\"not-a-module\"];\n");
    CHECK(imp.empty());
}

static void test_js_require_with_block_comment() {
    // Whitespace/comments between `require` and `(` must still be accepted
    auto imp = ts_imports_from("const m = require /* hi */ (\"mod\");\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_require_with_line_comment() {
    auto imp = ts_imports_from("const m = require // comment\n(\"mod\");\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_require_many_spaces() {
    auto imp = ts_imports_from("const m = require      (\"mod\");\n");
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "mod");
}

static void test_js_import_meta_glob_not_captured() {
    // `import.meta.glob("./**/*.ts")` must not become an import — the `.`
    // after `import` cancels the head state before the string is seen.
    auto imp = ts_imports_from("const files = import.meta.glob(\"./**/*.ts\");\n");
    CHECK(imp.empty());
}

static void test_js_import_meta_url_not_captured() {
    auto imp = ts_imports_from("const u = import.meta.url;\nconst s = \"nope\";\n");
    CHECK(imp.empty());
}

static void test_js_escaped_quotes_in_regular_string() {
    // A regular string with escaped quotes mentioning import-like text must
    // not produce a capture
    auto imp = ts_imports_from("console.log(\"import \\\"x\\\" from 'nope'\");\n");
    CHECK(imp.empty());
}

static void test_js_incomplete_import_no_crash() {
    // Truncated imports must not capture or crash
    auto imp = ts_imports_from("import\n");
    CHECK(imp.empty());
    auto imp2 = ts_imports_from("import { a, b\n");
    CHECK(imp2.empty());
    auto imp3 = ts_imports_from("import x from\n");
    CHECK(imp3.empty());
}

static void test_js_unterminated_string_no_crash() {
    // An unterminated string must not crash — scanner walks to EOF safely
    auto imp = ts_imports_from("const x = \"unterminated");
    CHECK(imp.empty());
}

static void test_js_unterminated_block_comment_no_crash() {
    // Similarly, an unterminated block comment must terminate cleanly
    auto imp = ts_imports_from("/* unterminated");
    CHECK(imp.empty());
}

static void test_js_no_state_leak_between_statements() {
    // After a bare `import x;` with ASI, a following string must not be
    // captured; the next real `import` still works.
    const char* src = "import x;\n"
                      "const y = \"nope\";\n"
                      "import z from \"real\";\n";
    auto imp = ts_imports_from(src);
    CHECK_EQ(imp.size(), 1u);
    if (!imp.empty())
        CHECK(imp[0] == "real");
}

static void test_js_destructured_from_binding_not_captured() {
    // `const { from } = obj` binds a local `from`; a later string must not
    // be captured
    auto imp = ts_imports_from("const { from } = obj;\nconst s = \"nope\";\n");
    CHECK(imp.empty());
}

static void test_js_export_const_string_not_captured() {
    // `export const msg = "hello"` is a declaration, not a re-export
    auto imp = ts_imports_from("export const msg = \"hello\";\n");
    CHECK(imp.empty());
}

// ---------------------------------------------------------------------------
// parse_directory
// ---------------------------------------------------------------------------

static void test_parse_directory_filters_extensions() {
    // Supported source files should be returned; unsupported extensions skipped.
    auto dir = g_tmp_root / "ext_filter";
    write_file(dir / "module.py", "x = 1\n");
    write_file(dir / "types.pyi", "def f() -> int: ...\n");
    write_file(dir / "module.ts", "export const x = 1;\n");
    write_file(dir / "side_effect.mjs", "import \"./module.js\";\n");
    write_file(dir / "notes.txt", "some text\n");
    write_file(dir / "helper.cpp", "int main(){}\n");

    auto files = rs::parse_directory(dir);
    CHECK_EQ(files.size(), 4u);

    auto has_name = [&](const std::string& name) {
        return std::any_of(files.begin(), files.end(), [&](const rs::ParsedFile& f) {
            return fs::path(f.path).filename().string() == name;
        });
    };
    CHECK(has_name("module.py"));
    CHECK(has_name("types.pyi"));
    CHECK(has_name("module.ts"));
    CHECK(has_name("side_effect.mjs"));
}

static void test_parse_directory_recursive() {
    // Subdirectories must be traversed; supported source files included.
    auto dir = g_tmp_root / "recursive";
    write_file(dir / "top.py", "import os\n");
    write_file(dir / "sub" / "nested.py", "import sys\n");
    write_file(dir / "sub" / "keep.js", "console.log('hi');\n");

    auto files = rs::parse_directory(dir);
    CHECK_EQ(files.size(), 3u);

    // Filesystem iteration order is unspecified — check membership by filename
    auto has_name = [&](const std::string& name) {
        return std::any_of(files.begin(), files.end(), [&](const rs::ParsedFile& f) {
            return fs::path(f.path).filename().string() == name;
        });
    };
    CHECK(has_name("top.py"));
    CHECK(has_name("nested.py"));
    CHECK(has_name("keep.js"));
}

static void test_parse_directory_returns_stable_sorted_paths() {
    auto dir = g_tmp_root / "stable_order";
    write_file(dir / "z_last.py", "import os\n");
    write_file(dir / "a_first.py", "import sys\n");
    write_file(dir / "mid" / "m_middle.ts", "export const x = 1;\n");

    auto files = rs::parse_directory(dir);
    CHECK_EQ(files.size(), 3u);

    std::vector<std::string> rels;
    rels.reserve(files.size());
    for (auto const& f : files) {
        rels.push_back(fs::path(f.path).lexically_relative(dir).generic_string());
    }

    CHECK(rels[0] == "a_first.py");
    CHECK(rels[1] == "mid/m_middle.ts");
    CHECK(rels[2] == "z_last.py");
}

// ---------------------------------------------------------------------------

int main() {
    g_tmp_root = fs::temp_directory_path() / "rs_test_parser";
    fs::remove_all(g_tmp_root);
    fs::create_directories(g_tmp_root);

    // import form
    test_import_single();
    test_import_multi_comma_with_alias();
    test_import_leading_spaces();
    test_import_leading_tab();

    // from … import form
    test_from_dotted_import();
    test_from_relative_single_dot();
    test_from_relative_double_dot();
    test_from_relative_single_dot_name();
    test_from_relative_triple_dot();
    test_import_dotted_preserves_full_path();
    test_import_multi_dotted_with_alias();

    // whitespace / comments
    test_inline_comment_ignored();
    test_empty_lines_ignored();
    test_comment_line_ignored();

    // deduplication
    test_duplicate_imports_deduped();

    // JS/TS import extraction
    test_js_default_import();
    test_js_named_import();
    test_js_namespace_import();
    test_js_mixed_default_and_named();
    test_js_bare_side_effect_import();
    test_js_type_only_import();
    test_js_export_star_from();
    test_js_export_named_from();
    test_js_require();
    test_js_dynamic_import();
    test_js_dynamic_import_template_literal_not_captured();
    test_js_multiline_import();
    test_js_relative_specifier_kept_verbatim();
    test_js_line_comment_ignored();
    test_js_block_comment_ignored();
    test_js_string_literal_false_positive_ignored();
    test_js_require_identifier_not_called();
    test_js_multiple_imports_and_dedup();
    test_js_extension_variants_parse_as_js();

    // JS/TS hardening
    test_js_from_method_call_not_captured();
    test_js_bare_from_call_not_captured();
    test_js_array_from_not_captured();
    test_js_method_require_not_captured();
    test_js_require_bracket_access_not_captured();
    test_js_require_with_block_comment();
    test_js_require_with_line_comment();
    test_js_require_many_spaces();
    test_js_import_meta_glob_not_captured();
    test_js_import_meta_url_not_captured();
    test_js_escaped_quotes_in_regular_string();
    test_js_incomplete_import_no_crash();
    test_js_unterminated_string_no_crash();
    test_js_unterminated_block_comment_no_crash();
    test_js_no_state_leak_between_statements();
    test_js_destructured_from_binding_not_captured();
    test_js_export_const_string_not_captured();

    // parse_directory
    test_parse_directory_filters_extensions();
    test_parse_directory_recursive();
    test_parse_directory_returns_stable_sorted_paths();

    fs::remove_all(g_tmp_root);

    const int total = 54;
    if (g_failures == 0) {
        std::printf("All %d tests passed.\n", total);
        return 0;
    }
    std::fprintf(stderr, "%d/%d test(s) FAILED.\n", g_failures, total);
    return 1;
}
