#pragma once

#include "parser.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rs {

struct TransparentStringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }

    std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct TransparentStringEq {
    using is_transparent = void;

    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

// ---------------------------------------------------------------------------
// Vocabulary — bidirectional term ↔ id mapping.
//
// We enable transparent hashing/equality so query-time lookups can pass
// std::string_view directly (no temporary std::string allocation in find()).
// ---------------------------------------------------------------------------
struct Vocabulary {
    std::unordered_map<std::string, uint32_t, TransparentStringHash, TransparentStringEq>
        term_to_id;
    std::vector<std::string> id_to_term;

    // Insert `tok` if absent; return its id either way.
    uint32_t intern(std::string_view tok);

    // Look up without inserting.  Returns ~0u if not found.
    uint32_t find(std::string_view tok) const;

    uint32_t size() const noexcept { return static_cast<uint32_t>(id_to_term.size()); }
};

// ---------------------------------------------------------------------------
// Posting — one entry in a term's postings list.
// TF is normalized: raw_count / doc_length.
// ---------------------------------------------------------------------------
struct Posting {
    uint32_t file_id;
    float tf;
};

// ---------------------------------------------------------------------------
// Index — the central data structure for Phase 2+.
//
// Ownership notes:
//   paths, file_imports  — moved out of ParsedFile during build_index()
//   postings             — term_id → sorted posting list (sorted by file_id)
//   doc_lengths          — used by callers that want to compute BM25 later
// ---------------------------------------------------------------------------
struct Index {
    Vocabulary vocab;
    std::vector<std::string> paths;                     // file_id → path
    std::vector<std::vector<std::string>> file_imports; // file_id → import names
    std::vector<std::vector<Posting>> postings;         // term_id → postings
    std::vector<uint32_t> doc_lengths;                  // file_id → token count
    uint32_t num_docs{0};
};

// Build a complete index.  `files` is consumed (paths and imports are moved).
//
// IMPORTANT: ParsedFile::token_views are string_views into ParsedFile::content.
// build_index() accesses them before moving content, so they remain valid here.
// Callers must not move ParsedFiles after tokenize() has been called on them
// unless their content strings are heap-allocated (> ~22 chars, beyond SSO).
Index build_index(std::vector<ParsedFile> files);

} // namespace rs
