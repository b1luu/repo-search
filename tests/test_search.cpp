#include "graph_builder.h"
#include "indexer.h"
#include "parser.h"
#include "search_engine.h"
#include "tokenizer.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                  \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        if ((a) != (b)) {                                                                          \
            std::fprintf(stderr, "FAIL  %s:%d  (%s) == (%s)\n", __FILE__, __LINE__, #a, #b);       \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)
// NOLINTEND(cppcoreguidelines-macro-usage)

// ---------------------------------------------------------------------------
// Fixture: three-file corpus
//
//   a.py  — heavy on "result" and "search" tokens; imports b
//   b.py  — moderate "result" tokens; no imports (leaf node)
//   c.py  — no "result" or "search" tokens; imports a
//
// Expected search("result") ranking:
//   1. a.py  (highest tf for "result"; also gets graph bonus from b adjacency)
//   2. b.py  (has "result"; gets graph bonus from a)
//   3. c.py  (zero lexical score; graph-expanded via import of a)
// ---------------------------------------------------------------------------

static rs::ParsedFile make_pf(std::string path, std::string content,
                              std::vector<std::string> imports = {}) {
    rs::ParsedFile pf;
    pf.path = std::move(path);
    pf.content = std::move(content);
    pf.imports = std::move(imports);
    pf.token_views = rs::tokenize(pf.content);
    return pf;
}

struct Corpus {
    rs::Index idx;
    rs::Graph graph;
};

static Corpus make_corpus() {
    // Content strings are intentionally > 22 chars to exceed SSO on libc++.
    // (See SSO hazard note in indexer.h.)
    std::vector<rs::ParsedFile> files;
    files.reserve(3);

    // file_id = 0
    files.push_back(make_pf("a.py",
                            "result search result result query computation analysis processing",
                            {"b"}));

    // file_id = 1
    files.push_back(
        make_pf("b.py", "result data storage buffer allocation management performance", {}));

    // file_id = 2 — NO "result" or "search" tokens; pure graph candidate
    files.push_back(make_pf("c.py",
                            "helper utility format output logging configuration settings batch",
                            {"a"}));

    auto idx = rs::build_index(std::move(files));
    auto graph = rs::build_graph(idx);
    return {std::move(idx), std::move(graph)};
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_empty_index_returns_empty() {
    rs::Index idx;
    rs::Graph graph;
    auto results = rs::search("result", idx, graph);
    CHECK(results.empty());
}

static void test_unknown_query_term_returns_empty() {
    auto [idx, graph] = make_corpus();
    auto results = rs::search("xyzxyzxyz", idx, graph);
    CHECK(results.empty());
}

static void test_basic_search_returns_results() {
    auto [idx, graph] = make_corpus();
    auto results = rs::search("result", idx, graph);
    CHECK(!results.empty());
}

static void test_highest_tf_file_ranks_first() {
    // a.py has 3 occurrences of "result" (highest tf in corpus).
    auto [idx, graph] = make_corpus();
    auto results = rs::search("result", idx, graph);

    CHECK(!results.empty());
    // The top result must be a.py (file_id 0).
    CHECK_EQ(results[0].file_id, 0u);
    CHECK(results[0].path == "a.py");
}

static void test_scores_descending() {
    auto [idx, graph] = make_corpus();
    auto results = rs::search("result", idx, graph);

    for (std::size_t i = 1; i < results.size(); ++i) {
        CHECK(results[i - 1].score >= results[i].score);
    }
}

static void test_graph_expansion_includes_pure_neighbor() {
    // c.py has zero lexical score for "result" but imports a.py.
    // It should appear in results via graph expansion.
    auto [idx, graph] = make_corpus();

    rs::SearchParams params;
    params.top_k = 10;
    params.graph_alpha = 0.15f;

    auto results = rs::search("result", idx, graph, params);

    bool found_c = false;
    for (auto const& r : results) {
        if (r.path == "c.py") {
            found_c = true;
            break;
        }
    }
    CHECK(found_c);
}

static void test_graph_expansion_disabled() {
    // With alpha = 0, c.py (no lexical match) must NOT appear in results.
    auto [idx, graph] = make_corpus();

    rs::SearchParams params;
    params.top_k = 10;
    params.graph_alpha = 0.0f;

    auto results = rs::search("result", idx, graph, params);

    for (auto const& r : results) {
        CHECK(r.path != "c.py");
    }
}

static void test_top_k_limits_results() {
    auto [idx, graph] = make_corpus();

    rs::SearchParams params;
    params.top_k = 1;

    auto results = rs::search("result", idx, graph, params);
    CHECK_EQ(results.size(), 1u);
}

static void test_multi_term_query() {
    // "result search" should still rank a.py first (has both terms).
    auto [idx, graph] = make_corpus();
    auto results = rs::search("result search", idx, graph);

    CHECK(!results.empty());
    CHECK_EQ(results[0].file_id, 0u);
}

static void test_result_scores_positive() {
    auto [idx, graph] = make_corpus();
    auto results = rs::search("result", idx, graph);

    for (auto const& r : results) {
        CHECK(r.score > 0.0f);
    }
}

// ---------------------------------------------------------------------------

int main() {
    test_empty_index_returns_empty();
    test_unknown_query_term_returns_empty();
    test_basic_search_returns_results();
    test_highest_tf_file_ranks_first();
    test_scores_descending();
    test_graph_expansion_includes_pure_neighbor();
    test_graph_expansion_disabled();
    test_top_k_limits_results();
    test_multi_term_query();
    test_result_scores_positive();

    const int total = 10;
    if (g_failures == 0) {
        std::printf("All %d tests passed.\n", total);
        return 0;
    }
    std::fprintf(stderr, "%d/%d test(s) FAILED.\n", g_failures, total);
    return 1;
}
