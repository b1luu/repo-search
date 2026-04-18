#pragma once

#include "indexer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rs {

// Repository-aware Python module lookup built from the indexed corpus.
//
// Responsibilities:
//   - map Python files to dotted module paths
//   - track whether a file represents a package (__init__.py)
//   - resolve absolute and relative Python imports to file_ids
//
// It is intentionally scoped to intra-corpus resolution only.
struct PythonModuleMap {
    std::unordered_map<std::string, uint32_t> module_to_id;
    std::vector<std::string> file_module;
    std::vector<bool> file_is_init;

    static PythonModuleMap build(const Index& idx);

    std::optional<uint32_t> resolve_import(uint32_t file_id, std::string_view import_str) const;
};

} // namespace rs
