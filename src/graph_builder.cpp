#include "graph_builder.h"

#include "source_path.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rs {

namespace fs = std::filesystem;

static std::string lowercase_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Python module-path resolution
//
// Maps each .py/.pyi file to its dotted module path relative to the corpus
// root, using directory structure:
//   pkg/sub/mod.py       → "pkg.sub.mod"
//   pkg/sub/__init__.py  → "pkg.sub"
//   pkg/__init__.py      → "pkg"
//   app.py               → "app"
//
// Relative imports (leading dots) are resolved against the importing file's
// package position before lookup.
// ---------------------------------------------------------------------------

// Compute the longest common directory prefix across all indexed paths.
// This serves as the corpus root for computing Python module paths.
static fs::path compute_corpus_root(const std::vector<std::string>& paths) {
    if (paths.empty())
        return {};

    fs::path root = fs::path(paths[0]).parent_path().lexically_normal();

    for (std::size_t i = 1; i < paths.size(); ++i) {
        fs::path dir = fs::path(paths[i]).parent_path().lexically_normal();
        fs::path common;
        auto r = root.begin();
        auto d = dir.begin();
        while (r != root.end() && d != dir.end() && *r == *d) {
            common /= *r;
            ++r;
            ++d;
        }
        root = common;
    }

    return root;
}

// Convert a Python file path to its dotted module path relative to `root`.
// Returns an empty string for files outside root or degenerate cases.
static std::string file_to_python_module(const fs::path& file, const fs::path& root) {
    fs::path rel = file.lexically_relative(root);
    if (rel.empty() || *rel.begin() == "..")
        return {};

    std::string module;
    for (auto it = rel.begin(); it != rel.end(); ++it) {
        auto next = it;
        ++next;
        if (next == rel.end()) {
            // This is the filename component.
            std::string stem = rel.stem().string();
            if (stem == "__init__") {
                // __init__.py represents the package directory itself —
                // the module path is the directory components already accumulated.
                break;
            }
            if (!module.empty())
                module += '.';
            module += stem;
        } else {
            if (!module.empty())
                module += '.';
            module += it->string();
        }
    }

    return module;
}

// Resolve a relative Python import (leading dots) to an absolute module path.
//   import_str:   the raw import string, e.g. ".sub", "..core", "."
//   file_module:  dotted module path of the importing file, e.g. "pkg.mod"
//   is_init:      true if the importing file is __init__.py
static std::string resolve_python_relative(const std::string& import_str,
                                           const std::string& file_module, bool is_init) {
    // Count leading dots.
    std::size_t dots = 0;
    while (dots < import_str.size() && import_str[dots] == '.')
        ++dots;

    // Determine the package context.  For __init__.py the file IS the
    // package; for regular .py files the package is the parent.
    std::string package = file_module;
    if (!is_init) {
        auto pos = package.rfind('.');
        package = (pos != std::string::npos) ? package.substr(0, pos) : "";
    }

    // Each additional dot beyond the first goes up one more level.
    for (std::size_t i = 1; i < dots; ++i) {
        auto pos = package.rfind('.');
        package = (pos != std::string::npos) ? package.substr(0, pos) : "";
    }

    std::string rest = (dots < import_str.size()) ? import_str.substr(dots) : "";

    if (rest.empty())
        return package;
    if (package.empty())
        return rest;
    return package + "." + rest;
}

// ---------------------------------------------------------------------------
// JS/TS relative-import resolution
//
// Given an importing file and a relative specifier ("./foo", "../lib/bar"),
// walk a prebuilt <normalized-path → file_id> map and try the candidates a
// Node/TS resolver would look at — minus tsconfig paths and node_modules,
// which would require reading project config files.
//
// Candidates, in order:
//   <base>.ts, .tsx, .js, .jsx, .mjs, .cjs
//   <base>/index.ts, .tsx, .js, .jsx, .mjs, .cjs
//   <base>                (if the user wrote the extension themselves)
//
// Limitations:
//   - No tsconfig `paths` or `baseUrl` aliases.
//   - No bare-specifier resolution (e.g. "react") — intentionally dropped.
//   - No package.json `main`/`exports` fields.
//   - Case-sensitive string comparison of the normalized paths.
// ---------------------------------------------------------------------------
static std::optional<uint32_t> resolve_js_relative(
    const fs::path& from_file, std::string_view module_str,
    const std::unordered_map<std::string, uint32_t>& path_to_id) {
    if (module_str.empty() || module_str[0] != '.')
        return std::nullopt;

    const fs::path base = normalize_source_path(from_file.parent_path() / std::string(module_str));

    // 1. base + extension
    for (std::string_view ext : {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"}) {
        fs::path cand = base;
        cand += ext;
        const auto it = path_to_id.find(normalized_source_key(cand));
        if (it != path_to_id.end())
            return it->second;
    }

    // 2. base/index.<ext>  (directory-as-module convention)
    for (std::string_view ext : {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"}) {
        const fs::path cand = base / (std::string("index") + std::string(ext));
        const auto it = path_to_id.find(normalized_source_key(cand));
        if (it != path_to_id.end())
            return it->second;
    }

    // 3. base exactly as written (user supplied the extension)
    const auto it = path_to_id.find(normalized_source_key(base));
    if (it != path_to_id.end())
        return it->second;

    return std::nullopt;
}

Graph build_graph(const Index& idx) {
    Graph g;
    g.num_nodes = idx.num_docs;
    g.adj.resize(idx.num_docs);
    g.radj.resize(idx.num_docs);

    // -----------------------------------------------------------------------
    // Compute the corpus root for Python module-path resolution.
    // -----------------------------------------------------------------------
    const fs::path corpus_root = compute_corpus_root(idx.paths);

    // -----------------------------------------------------------------------
    // Build lookup tables in a single pass over the corpus:
    //   python_module_to_id — dotted module path → fid
    //   python_file_module  — fid → module path (for resolving relative imports)
    //   python_is_init      — fid → true if file is __init__.py
    //   js_path_to_id       — full normalized path → fid
    // -----------------------------------------------------------------------
    std::unordered_map<std::string, uint32_t> python_module_to_id;
    std::vector<std::string> python_file_module(idx.num_docs);
    std::vector<bool> python_is_init(idx.num_docs, false);
    std::unordered_map<std::string, uint32_t> js_path_to_id;
    python_module_to_id.reserve(idx.num_docs);
    js_path_to_id.reserve(idx.num_docs);

    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        const fs::path p(idx.paths[fid]);
        const SourceLang lang = classify_source_path(p);

        if (lang == SourceLang::python) {
            std::string mod = lowercase_copy(file_to_python_module(p, corpus_root));
            if (!mod.empty()) {
                python_module_to_id.try_emplace(mod, fid);
            }
            python_file_module[fid] = std::move(mod);
            python_is_init[fid] = (p.stem().string() == "__init__");
        } else if (lang == SourceLang::js_ts) {
            js_path_to_id[normalized_source_key(p)] = fid;
        }
    }

    // -----------------------------------------------------------------------
    // Resolve each file's import list to file_ids and append edges.
    //   Python: full dotted module-path lookup; relative imports resolved
    //           against the importing file's package position
    //   JS/TS:  resolve relative specifiers against js_path_to_id;
    //           bare specifiers (npm packages) are dropped silently
    // -----------------------------------------------------------------------
    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        const fs::path p(idx.paths[fid]);
        const SourceLang lang = classify_source_path(p);
        const bool python = lang == SourceLang::python;
        const bool jsts = lang == SourceLang::js_ts;

        for (std::string const& imp : idx.file_imports[fid]) {
            std::optional<uint32_t> target;

            if (python) {
                std::string resolved;
                if (!imp.empty() && imp[0] == '.') {
                    // Relative import — resolve against file's package position.
                    resolved = lowercase_copy(
                        resolve_python_relative(imp, python_file_module[fid], python_is_init[fid]));
                } else {
                    resolved = lowercase_copy(imp);
                }
                if (!resolved.empty()) {
                    const auto it = python_module_to_id.find(resolved);
                    if (it != python_module_to_id.end())
                        target = it->second;
                }
            } else if (jsts) {
                if (!imp.empty() && imp[0] == '.') {
                    target = resolve_js_relative(p, imp, js_path_to_id);
                }
            }

            if (!target)
                continue;
            if (*target == fid)
                continue;

            g.adj[fid].push_back(*target);
            g.radj[*target].push_back(fid);
        }
    }

    // Deduplicate edges (a file can mention the same import more than once).
    for (auto& neighbors : g.adj) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    for (auto& neighbors : g.radj) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }

    return g;
}

} // namespace rs
