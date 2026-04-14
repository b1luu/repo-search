#include "graph_builder.h"
#include "indexer.h"
#include "parser.h"
#include "search_engine.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// Phase 2 driver
// Pipeline: parse → index → graph → search
// ---------------------------------------------------------------------------

static double elapsed_ms(std::chrono::steady_clock::time_point a,
                         std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <directory> <query>\n", argv[0]);
        return 1;
    }

    const std::filesystem::path root(argv[1]);
    if (!std::filesystem::exists(root)) {
        std::fprintf(stderr, "Error: '%s' does not exist.\n", argv[1]);
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();

    // ---- Parse --------------------------------------------------------------
    auto files = rs::parse_directory(root);
    auto t1 = std::chrono::steady_clock::now();
    std::printf("Parse:  %zu files  (%.1f ms)\n", files.size(), elapsed_ms(t0, t1));

    // ---- Index --------------------------------------------------------------
    auto idx = rs::build_index(std::move(files));
    auto t2 = std::chrono::steady_clock::now();
    std::printf("Index:  %u docs, %u terms  (%.1f ms)\n", idx.num_docs, idx.vocab.size(),
                elapsed_ms(t1, t2));

    // ---- Graph --------------------------------------------------------------
    auto graph = rs::build_graph(idx);
    auto t3 = std::chrono::steady_clock::now();

    std::size_t total_edges = 0;
    for (auto const& nbrs : graph.adj)
        total_edges += nbrs.size();
    std::printf("Graph:  %zu intra-corpus edges  (%.1f ms)\n", total_edges, elapsed_ms(t2, t3));

    // ---- Search -------------------------------------------------------------
    const std::string query(argv[2]);
    auto t4 = std::chrono::steady_clock::now();
    auto results = rs::search(query, idx, graph);
    auto t5 = std::chrono::steady_clock::now();

    const double search_us = std::chrono::duration<double, std::micro>(t5 - t4).count();

    std::printf("\nSearch: \"%s\"  (%.1f µs)\n\n", query.c_str(), search_us);
    std::printf("Top results:\n");

    if (results.empty()) {
        std::printf("  (no results)\n");
    } else {
        for (auto const& r : results) {
            std::printf("  [%.4f] %.*s\n", r.score, static_cast<int>(r.path.size()), r.path.data());
        }
    }

    return 0;
}
