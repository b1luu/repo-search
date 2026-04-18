#include "search_engine.h"

#include "tokenizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rs {

static bool higher_rank(const SearchResult& a, const SearchResult& b) {
    if (a.score != b.score)
        return a.score > b.score;
    if (a.path != b.path)
        return a.path < b.path;
    return a.file_id < b.file_id;
}

std::vector<SearchResult> search(std::string_view query, const Index& idx, const Graph& graph,
                                 const SearchParams& params) {
    if (idx.num_docs == 0 || params.top_k == 0)
        return {};

    // -----------------------------------------------------------------------
    // 1. Tokenize and resolve query terms.
    // -----------------------------------------------------------------------
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

    if (term_ids.empty())
        return {};

    // -----------------------------------------------------------------------
    // 2. TF-IDF scoring.
    //
    // Use a flat vector indexed by file_id — O(1) access, cache-friendly,
    // no hashing overhead.  The vector is num_docs floats = a few KB at most.
    // -----------------------------------------------------------------------
    std::vector<float> lex_scores(idx.num_docs, 0.0f);
    std::vector<bool> is_candidate(idx.num_docs, false);
    std::vector<uint32_t> candidates;

    const float N = static_cast<float>(idx.num_docs);

    for (uint32_t tid : term_ids) {
        const auto& postings = idx.postings[tid];
        // Smooth IDF — denominators +1 prevent log(0) and dampen terms that
        // appear in nearly every document.
        const float idf = std::log((N + 1.0f) / (static_cast<float>(postings.size()) + 1.0f));

        for (auto const& p : postings) {
            lex_scores[p.file_id] += p.tf * idf;
            if (!is_candidate[p.file_id]) {
                is_candidate[p.file_id] = true;
                candidates.push_back(p.file_id);
            }
        }
    }

    // -----------------------------------------------------------------------
    // 3. Graph expansion — 1-hop reranking.
    //
    // For each lexical candidate, propagate a fraction of its score to
    // adjacent files in both directions:
    //   adj[fid]  = files that fid imports   ("fid uses these")
    //   radj[fid] = files that import fid    ("these use fid")
    //
    // Expansion is additive: a file can accumulate bonuses from multiple
    // candidates.  We use the original lex_scores — not the boosted ones —
    // so there is no circular amplification.
    //
    // Files that receive only a graph bonus (zero lexical score) are collected
    // separately and appear after lexical candidates in the unsorted list.
    // -----------------------------------------------------------------------
    std::vector<float> graph_bonus(idx.num_docs, 0.0f);
    std::vector<uint32_t> expanded_only; // neighbors with no lexical match

    const float alpha = params.graph_alpha;
    if (alpha > 0.0f) {
        std::vector<bool> is_expanded(idx.num_docs, false);

        for (uint32_t fid : candidates) {
            const float s = lex_scores[fid] * alpha;
            if (s <= 0.0f)
                continue;

            for (uint32_t nbr : graph.adj[fid]) {
                graph_bonus[nbr] += s;
                if (!is_candidate[nbr] && !is_expanded[nbr]) {
                    is_expanded[nbr] = true;
                    expanded_only.push_back(nbr);
                }
            }
            for (uint32_t nbr : graph.radj[fid]) {
                graph_bonus[nbr] += s;
                if (!is_candidate[nbr] && !is_expanded[nbr]) {
                    is_expanded[nbr] = true;
                    expanded_only.push_back(nbr);
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // 4. Collect results and return top-k.
    // -----------------------------------------------------------------------
    std::vector<SearchResult> results;
    results.reserve(candidates.size() + expanded_only.size());

    for (uint32_t fid : candidates) {
        results.push_back({lex_scores[fid] + graph_bonus[fid], fid, idx.paths[fid]});
    }
    for (uint32_t fid : expanded_only) {
        results.push_back({graph_bonus[fid], fid, idx.paths[fid]});
    }

    // partial_sort: O(n log k) — more efficient than full sort when k << n.
    const auto k = static_cast<std::ptrdiff_t>(std::min<std::size_t>(params.top_k, results.size()));
    std::partial_sort(results.begin(), results.begin() + k, results.end(), higher_rank);
    results.resize(static_cast<std::size_t>(k));

    return results;
}

} // namespace rs
