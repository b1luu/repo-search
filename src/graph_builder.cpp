#include "graph_builder.h"

#include "js_modules.h"
#include "python_modules.h"
#include "source_path.h"

#include <algorithm>
#include <optional>

namespace rs {

Graph build_graph(const Index& idx) {
    Graph g;
    g.num_nodes = idx.num_docs;
    g.adj.resize(idx.num_docs);
    g.radj.resize(idx.num_docs);

    // -----------------------------------------------------------------------
    // Build lookup tables in a single pass over the corpus:
    //   python_modules — intra-corpus Python module resolver
    //   js_modules     — intra-corpus JS/TS relative-specifier resolver
    // -----------------------------------------------------------------------
    const PythonModuleMap python_modules = PythonModuleMap::build(idx);
    const JsModuleMap js_modules = JsModuleMap::build(idx);

    // -----------------------------------------------------------------------
    // Resolve each file's import list to file_ids and append edges.
    //   Python: full dotted module-path lookup; relative imports resolved
    //           against the importing file's package position
    //   JS/TS:  resolve relative specifiers against the JS module map;
    //           bare specifiers (npm packages) are dropped silently
    // -----------------------------------------------------------------------
    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        const SourceLang lang = classify_source_path(std::filesystem::path(idx.paths[fid]));
        const bool python = lang == SourceLang::python;
        const bool jsts = lang == SourceLang::js_ts;

        for (std::string const& imp : idx.file_imports[fid]) {
            std::optional<uint32_t> target;

            if (python) {
                target = python_modules.resolve_import(fid, imp);
            } else if (jsts) {
                if (!imp.empty() && imp[0] == '.') {
                    target = js_modules.resolve_relative(fid, imp, idx);
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
