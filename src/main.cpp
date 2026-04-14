#include "parser.h"
#include "tokenizer.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// Phase 1 driver — parse a directory and print a summary.
// Subsequent phases will add: indexer, graph_builder, search_engine, reranker.
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <directory> [query]\n", argv[0]);
        return 1;
    }

    const std::filesystem::path root(argv[1]);
    if (!std::filesystem::exists(root)) {
        std::fprintf(stderr, "Error: directory '%s' does not exist.\n", argv[1]);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Parse
    // -----------------------------------------------------------------------
    const auto t0 = std::chrono::steady_clock::now();

    auto files = rs::parse_directory(root);

    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    std::size_t total_tokens  = 0;
    std::size_t total_imports = 0;
    for (auto const& pf : files) {
        total_tokens  += pf.token_views.size();
        total_imports += pf.imports.size();
    }

    std::printf("Parsed %zu .py files in %.2f ms\n", files.size(), ms);
    std::printf("Total tokens:  %zu\n", total_tokens);
    std::printf("Total imports: %zu\n", total_imports);

    // -----------------------------------------------------------------------
    // Sample: print first 10 files with their imports
    // -----------------------------------------------------------------------
    std::printf("\n--- Sample (first 10 files) ---\n");
    const std::size_t limit = std::min<std::size_t>(10, files.size());
    for (std::size_t i = 0; i < limit; ++i) {
        auto const& pf = files[i];
        std::printf("\n[%zu] %s\n", i, pf.path.c_str());
        std::printf("     tokens: %zu\n", pf.token_views.size());
        if (!pf.imports.empty()) {
            std::printf("     imports:");
            for (auto const& imp : pf.imports) std::printf(" %s", imp.c_str());
            std::printf("\n");
        }
    }

    // -----------------------------------------------------------------------
    // Optional: naive token-presence search (before indexer exists)
    // -----------------------------------------------------------------------
    if (argc >= 3) {
        const std::string query_raw(argv[2]);
        auto query_tokens = rs::tokenize_owned(query_raw);

        std::printf("\n--- Search: \"%s\" ---\n", query_raw.c_str());

        struct Hit { double score; std::string_view path; };
        std::vector<Hit> hits;

        for (auto const& pf : files) {
            double score = 0.0;
            for (auto const& qt : query_tokens) {
                for (auto const& tok : pf.token_views) {
                    if (tok == qt) score += 1.0;
                }
            }
            if (score > 0.0) hits.push_back({score, pf.path});
        }

        std::sort(hits.begin(), hits.end(),
                  [](auto const& a, auto const& b) { return a.score > b.score; });

        const std::size_t top_k = std::min<std::size_t>(10, hits.size());
        for (std::size_t i = 0; i < top_k; ++i) {
            std::printf("[%.0f] %s\n", hits[i].score, std::string(hits[i].path).c_str());
        }

        if (hits.empty()) std::printf("No results.\n");
    }

    return 0;
}
