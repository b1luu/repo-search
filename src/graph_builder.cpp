#include "graph_builder.h"

#include "python_modules.h"
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
    // Build lookup tables in a single pass over the corpus:
    //   python_modules — intra-corpus Python module resolver
    //   js_path_to_id       — full normalized path → fid
    // -----------------------------------------------------------------------
    const PythonModuleMap python_modules = PythonModuleMap::build(idx);
    std::unordered_map<std::string, uint32_t> js_path_to_id;
    js_path_to_id.reserve(idx.num_docs);

    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        const fs::path p(idx.paths[fid]);
        const SourceLang lang = classify_source_path(p);

        if (lang == SourceLang::js_ts) {
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
                target = python_modules.resolve_import(fid, imp);
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
