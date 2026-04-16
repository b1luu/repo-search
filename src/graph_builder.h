#pragma once

#include "indexer.h"

#include <cstdint>
#include <vector>

namespace rs {

// ---------------------------------------------------------------------------
// Graph — import dependency graph over the indexed file corpus.
//
// adj[fid]  = list of file_ids that fid imports   (outgoing / "uses")
// radj[fid] = list of file_ids that import fid    (incoming / "used by")
//
// Both lists are deduplicated and sorted ascending.
// Edges that target files outside the corpus (std lib, third-party packages)
// are silently dropped — only intra-corpus edges matter for reranking.
// ---------------------------------------------------------------------------
struct Graph {
    std::vector<std::vector<uint32_t>> adj;
    std::vector<std::vector<uint32_t>> radj;
    uint32_t num_nodes{0};
};

// Build the import dependency graph from an already-built Index.
// Python resolution: dotted module-path matching against directory structure.
// Relative imports (.foo, ..bar) are resolved from the importing file's package.
// JS/TS resolution: relative path resolution with extension/index probing.
Graph build_graph(const Index& idx);

} // namespace rs
