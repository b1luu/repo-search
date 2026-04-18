#include "search_engine.h"

#include "tokenizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rs {

namespace {

// Per-query mutable scoring state shared across the TF-IDF and graph-expansion
// phases.  Kept local to search_engine so helpers below don't leak into the
// public header.
struct ScoringState {
    std::vector<float> lex_scores;
    std::vector<bool> is_candidate;
    std::vector<uint32_t> candidates;
    std::vector<float> graph_bonus;
    std::vector<uint32_t> expanded_only;

    explicit ScoringState(uint32_t num_docs)
        : lex_scores(num_docs, 0.0f), is_candidate(num_docs, false),
          graph_bonus(num_docs, 0.0f) {}
};

// ---------------------------------------------------------------------------
// Phase 1 — tokenize the query and map surviving terms to vocabulary ids.
// ---------------------------------------------------------------------------
std::vector<uint32_t> resolve_query_terms(std::string_view query, const Index& idx) {
    auto query_tokens = tokenize_owned(query);

    // Deduplicate so each term contributes at most one IDF weight.
    std::sort(query_tokens.begin(), query_tokens.end());
    query_tokens.erase(std::unique(query_tokens.begin(), query_tokens.end()), query_tokens.end());

    std::vector<uint32_t> term_ids;
    term_ids.reserve(query_tokens.size());
    for (auto const& tok : query_tokens) {
        const uint32_t tid = idx.vocab.find(tok);
        if (tid != ~uint32_t{0})
            term_ids.push_back(tid);
    }
    return term_ids;
}

// ---------------------------------------------------------------------------
// Phase 2 — TF-IDF scoring.
//
// Writes per-doc lexical scores and tracks which documents matched at least
// one term (the candidate set).  Flat vectors keep access O(1) and
// cache-friendly; num_docs floats is only a few KB for typical corpora.
// ---------------------------------------------------------------------------
void score_lexical(const std::vector<uint32_t>& term_ids, const Index& idx, ScoringState& state) {
    const float N = static_cast<float>(idx.num_docs);

    for (uint32_t tid : term_ids) {
        const auto& postings = idx.postings[tid];
        // Smooth IDF — denominators +1 prevent log(0) and dampen terms that
        // appear in nearly every document.
        const float idf = std::log((N + 1.0f) / (static_cast<float>(postings.size()) + 1.0f));

        for (auto const& p : postings) {
            state.lex_scores[p.file_id] += p.tf * idf;
            if (!state.is_candidate[p.file_id]) {
                state.is_candidate[p.file_id] = true;
                state.candidates.push_back(p.file_id);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 3 — 1-hop graph expansion.
//
// For each lexical candidate, propagate a fraction of its score to adjacent
// files in both directions:
//   adj[fid]  = files that fid imports   ("fid uses these")
//   radj[fid] = files that import fid    ("these use fid")
//
// Expansion is additive: a file can accumulate bonuses from multiple
// candidates.  The original lex_scores are used (not the boosted ones) so
// there is no circular amplification.  Neighbors with no lexical match are
// collected separately.
// ---------------------------------------------------------------------------
void expand_via_graph(const Graph& graph, float alpha, ScoringState& state) {
    if (alpha <= 0.0f)
        return;

    std::vector<bool> is_expanded(state.lex_scores.size(), false);

    auto note_neighbor = [&](uint32_t nbr, float s) {
        state.graph_bonus[nbr] += s;
        if (!state.is_candidate[nbr] && !is_expanded[nbr]) {
            is_expanded[nbr] = true;
            state.expanded_only.push_back(nbr);
        }
    };

    for (uint32_t fid : state.candidates) {
        const float s = state.lex_scores[fid] * alpha;
        if (s <= 0.0f)
            continue;

        for (uint32_t nbr : graph.adj[fid])
            note_neighbor(nbr, s);
        for (uint32_t nbr : graph.radj[fid])
            note_neighbor(nbr, s);
    }
}

// ---------------------------------------------------------------------------
// Phase 4 — collect and rank top-k results with deterministic tie-breaking.
// ---------------------------------------------------------------------------
bool higher_rank(const SearchResult& a, const SearchResult& b) {
    if (a.score != b.score)
        return a.score > b.score;
    if (a.path != b.path)
        return a.path < b.path;
    return a.file_id < b.file_id;
}

std::vector<SearchResult> collect_top_k(const Index& idx, const ScoringState& state,
                                        uint32_t top_k) {
    std::vector<SearchResult> results;
    results.reserve(state.candidates.size() + state.expanded_only.size());

    for (uint32_t fid : state.candidates) {
        results.push_back({state.lex_scores[fid] + state.graph_bonus[fid], fid, idx.paths[fid]});
    }
    for (uint32_t fid : state.expanded_only) {
        results.push_back({state.graph_bonus[fid], fid, idx.paths[fid]});
    }

    // partial_sort: O(n log k) — more efficient than full sort when k << n.
    const auto k = static_cast<std::ptrdiff_t>(std::min<std::size_t>(top_k, results.size()));
    std::partial_sort(results.begin(), results.begin() + k, results.end(), higher_rank);
    results.resize(static_cast<std::size_t>(k));
    return results;
}

} // namespace

std::vector<SearchResult> search(std::string_view query, const Index& idx, const Graph& graph,
                                 const SearchParams& params) {
    if (idx.num_docs == 0 || params.top_k == 0)
        return {};

    const auto term_ids = resolve_query_terms(query, idx);
    if (term_ids.empty())
        return {};

    ScoringState state(idx.num_docs);
    score_lexical(term_ids, idx, state);
    expand_via_graph(graph, params.graph_alpha, state);
    return collect_top_k(idx, state, params.top_k);
}

} // namespace rs
