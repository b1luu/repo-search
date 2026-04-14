#include "graph_builder.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rs {

namespace fs = std::filesystem;

static std::string lowercase_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return s;
}

static bool is_python_ext(std::string_view ext) {
    return ext == ".py" || ext == ".pyi";
}

static bool is_jsts_ext(std::string_view ext) {
    return ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx" || ext == ".mjs" ||
           ext == ".cjs";
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

    const fs::path base = (from_file.parent_path() / std::string(module_str)).lexically_normal();

    static constexpr const char* kExts[] = {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"};

    // 1. base + extension
    for (const char* ext : kExts) {
        fs::path cand = base;
        cand += ext;
        const auto it = path_to_id.find(cand.generic_string());
        if (it != path_to_id.end())
            return it->second;
    }

    // 2. base/index.<ext>  (directory-as-module convention)
    for (const char* ext : kExts) {
        const fs::path cand = base / (std::string("index") + ext);
        const auto it = path_to_id.find(cand.generic_string());
        if (it != path_to_id.end())
            return it->second;
    }

    // 3. base exactly as written (user supplied the extension)
    const auto it = path_to_id.find(base.generic_string());
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
    // Build two lookups in a single pass over the corpus:
    //   python_module_to_id — stem (or package name for __init__.py) → fid
    //   js_path_to_id       — full normalized path → fid, used for resolving
    //                         JS/TS relative import specifiers
    // -----------------------------------------------------------------------
    std::unordered_map<std::string, uint32_t> python_module_to_id;
    std::unordered_map<std::string, uint32_t> js_path_to_id;
    python_module_to_id.reserve(idx.num_docs);
    js_path_to_id.reserve(idx.num_docs);

    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        const fs::path p(idx.paths[fid]);
        const std::string ext = p.extension().string();

        if (is_python_ext(ext)) {
            const std::string stem = p.stem().string();
            if (stem == "__init__") {
                python_module_to_id.try_emplace(lowercase_copy(p.parent_path().filename().string()),
                                                fid);
            } else {
                python_module_to_id.try_emplace(lowercase_copy(stem), fid);
            }
        } else if (is_jsts_ext(ext)) {
            js_path_to_id[p.lexically_normal().generic_string()] = fid;
        }
    }

    // -----------------------------------------------------------------------
    // Resolve each file's import list to file_ids and append edges.
    // Language-specific dispatch:
    //   Python: stem match (existing behavior)
    //   JS/TS:  resolve relative specifiers against js_path_to_id;
    //           bare specifiers (npm packages) are dropped silently
    // -----------------------------------------------------------------------
    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        const fs::path p(idx.paths[fid]);
        const std::string ext = p.extension().string();
        const bool python = is_python_ext(ext);
        const bool jsts = is_jsts_ext(ext);

        for (std::string const& imp : idx.file_imports[fid]) {
            std::optional<uint32_t> target;

            if (python) {
                const std::string lower = lowercase_copy(imp);
                const auto it = python_module_to_id.find(lower);
                if (it != python_module_to_id.end())
                    target = it->second;
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
