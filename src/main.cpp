#include "graph_builder.h"
#include "indexer.h"
#include "parser.h"
#include "search_engine.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// Phase 2 driver
// Pipeline: parse → index → graph → search
// ---------------------------------------------------------------------------

static double elapsed_ms(std::chrono::steady_clock::time_point a,
                         std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static bool parse_uint(std::string_view s, uint32_t& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

static bool parse_float(std::string_view s, float& out) {
    // std::from_chars<float> isn't universally available on older libc++; use strtof.
    std::string buf(s);
    char* end = nullptr;
    out = std::strtof(buf.c_str(), &end);
    return end != buf.c_str() && *end == '\0';
}

static void print_usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s [--top-k N] [--alpha F] <directory> <query>\n"
                 "  --top-k N   number of results to return (default 10)\n"
                 "  --alpha F   graph expansion weight in [0,1] (default 0.15)\n",
                 prog);
}

int main(int argc, char* argv[]) {
    rs::SearchParams params{};
    const char* dir_arg = nullptr;
    const char* query_arg = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        auto take_value = [&](std::string_view flag, std::string_view& val) -> bool {
            if (a == flag) {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "Error: %.*s requires a value.\n",
                                 static_cast<int>(flag.size()), flag.data());
                    return false;
                }
                val = argv[++i];
                return true;
            }
            if (a.size() > flag.size() + 1 && a.substr(0, flag.size()) == flag &&
                a[flag.size()] == '=') {
                val = a.substr(flag.size() + 1);
                return true;
            }
            return false;
        };

        std::string_view val;
        if (a == "--top-k" || a.substr(0, 8) == "--top-k=") {
            if (!take_value("--top-k", val))
                return 1;
            if (!parse_uint(val, params.top_k) || params.top_k == 0) {
                std::fprintf(stderr, "Error: --top-k expects a positive integer.\n");
                return 1;
            }
        } else if (a == "--alpha" || a.substr(0, 8) == "--alpha=") {
            if (!take_value("--alpha", val))
                return 1;
            if (!parse_float(val, params.graph_alpha) || params.graph_alpha < 0.0f ||
                params.graph_alpha > 1.0f) {
                std::fprintf(stderr, "Error: --alpha expects a float in [0,1].\n");
                return 1;
            }
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "Error: unknown flag '%.*s'.\n", static_cast<int>(a.size()),
                         a.data());
            print_usage(argv[0]);
            return 1;
        } else if (!dir_arg) {
            dir_arg = argv[i];
        } else if (!query_arg) {
            query_arg = argv[i];
        } else {
            std::fprintf(stderr, "Error: unexpected positional '%s'.\n", argv[i]);
            return 1;
        }
    }

    if (!dir_arg || !query_arg) {
        print_usage(argv[0]);
        return 1;
    }

    const std::filesystem::path root(dir_arg);
    if (!std::filesystem::exists(root)) {
        std::fprintf(stderr, "Error: '%s' does not exist.\n", dir_arg);
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
    const std::string query(query_arg);
    auto t4 = std::chrono::steady_clock::now();
    auto results = rs::search(query, idx, graph, params);
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
