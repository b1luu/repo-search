#include "graph_builder.h"

#include "import_resolver.h"

#include <algorithm>

namespace rs {

Graph build_graph(const Index& idx) {
    Graph g;
    g.num_nodes = idx.num_docs;
    g.adj.resize(idx.num_docs);
    g.radj.resize(idx.num_docs);

    const ImportResolver resolver = ImportResolver::build(idx);

    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        for (std::string const& imp : idx.file_imports[fid]) {
            const auto target = resolver.resolve(fid, imp);

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
