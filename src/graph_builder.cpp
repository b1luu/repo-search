#include "graph_builder.h"

#include "import_resolver.h"

#include <algorithm>

namespace rs {

static void add_edge(Graph& g, uint32_t from, uint32_t to) {
    if (from == to)
        return;
    g.adj[from].push_back(to);
    g.radj[to].push_back(from);
}

static void sort_and_dedupe_neighbors(std::vector<std::vector<uint32_t>>& adjacency) {
    for (auto& neighbors : adjacency) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
}

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
            add_edge(g, fid, *target);
        }
    }

    // Preserve the graph invariant that adjacency lists are sorted and unique.
    sort_and_dedupe_neighbors(g.adj);
    sort_and_dedupe_neighbors(g.radj);

    return g;
}

} // namespace rs
