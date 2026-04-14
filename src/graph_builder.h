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
// Resolution strategy: map each import name to a file whose stem matches.
// __init__.py files are mapped to their parent directory name (package root).
Graph build_graph(const Index& idx);

} // namespace rs
