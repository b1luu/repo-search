#include "indexer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace rs {

// ---------------------------------------------------------------------------
// Vocabulary
// ---------------------------------------------------------------------------

uint32_t Vocabulary::intern(std::string_view tok) {
    if (auto it = term_to_id.find(tok); it != term_to_id.end()) {
        return it->second;
    }

    // try_emplace avoids double-hashing on insert: the key is constructed
    // once, and if insertion happens the map node owns it directly.
    auto [it, inserted] =
        term_to_id.try_emplace(std::string(tok), static_cast<uint32_t>(id_to_term.size()));
    if (inserted) {
        // Reuse the key already stored in the node — no second allocation.
        id_to_term.push_back(it->first);
    }
    return it->second;
}

uint32_t Vocabulary::find(std::string_view tok) const {
    auto it = term_to_id.find(tok);
    return (it != term_to_id.end()) ? it->second : ~uint32_t{0};
}

// ---------------------------------------------------------------------------
// build_index
// ---------------------------------------------------------------------------

Index build_index(std::vector<ParsedFile> files) {
    Index idx;
    const auto num_docs = static_cast<uint32_t>(files.size());
    idx.num_docs = num_docs;
    idx.paths.reserve(num_docs);
    idx.file_imports.reserve(num_docs);
    idx.doc_lengths.reserve(num_docs);

    // Reuse this map across files to amortize allocations.
    // Maps term_id → raw occurrence count within the current document.
    std::unordered_map<uint32_t, uint32_t> tf_counts;
    tf_counts.reserve(512); // typical distinct terms per file

    for (uint32_t fid = 0; fid < num_docs; ++fid) {
        ParsedFile& pf = files[fid];

        // Move metadata out before touching token_views (which are views into
        // pf.content — we must not move content while the views are in use).
        idx.paths.push_back(std::move(pf.path));
        idx.file_imports.push_back(std::move(pf.imports));
        const uint32_t doc_len = static_cast<uint32_t>(pf.token_views.size());
        idx.doc_lengths.push_back(doc_len);

        // Count term frequencies for this document.
        tf_counts.clear();
        for (std::string_view tok : pf.token_views) {
            tf_counts[idx.vocab.intern(tok)]++;
        }
        // pf.content may now be safely released (token_views no longer needed).

        // Grow postings array to match any new vocabulary entries.
        const uint32_t vocab_sz = idx.vocab.size();
        if (vocab_sz > static_cast<uint32_t>(idx.postings.size())) {
            idx.postings.resize(vocab_sz);
        }

        // Append one Posting per distinct term in this document.
        const float inv_len = (doc_len > 0) ? (1.0f / static_cast<float>(doc_len)) : 0.0f;
        for (auto const& [tid, count] : tf_counts) {
            idx.postings[tid].push_back({fid, static_cast<float>(count) * inv_len});
        }
        // Each postings list is appended in ascending file_id order because
        // we process files sequentially — no explicit sort needed.
    }

    return idx;
}

} // namespace rs
