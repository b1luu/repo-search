#pragma once

#include "graph_builder.h"
#include "indexer.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace rs {

struct SearchResult {
    float score;
    uint32_t file_id;
    std::string_view path; // view into Index::paths — valid for the Index's lifetime
};

struct SearchParams {
    uint32_t top_k{10};
    // Fraction of a lexical candidate's score propagated to 1-hop graph
    // neighbors.  0 disables graph expansion entirely.
    float graph_alpha{0.15f};
};

// Full query pipeline:
//   tokenize → term lookup → TF-IDF accumulation →
//   1-hop graph expansion (reranking) → top-k
//
// Scoring:
//   lex_score(file)  = Σ_t  tf(t, file) * idf(t)
//   idf(t)           = log((N + 1) / (df(t) + 1))   [smooth Laplace IDF]
//   graph_bonus(file)= alpha * Σ_{nbr ∈ adj∪radj}  lex_score(nbr)
//   final_score      = lex_score + graph_bonus
//
// Files with zero lexical score but non-zero graph bonus are included in
// results — they are structurally related to matching files even if they
// don't contain the query tokens themselves.
std::vector<SearchResult> search(std::string_view query, const Index& idx, const Graph& graph,
                                 const SearchParams& params = {});

} // namespace rs
