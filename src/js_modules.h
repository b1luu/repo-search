#pragma once

#include "indexer.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace rs {

// Intra-corpus JS/TS module resolver.
//
// Responsibilities:
//   - map normalized JS/TS file paths to file_ids
//   - resolve relative specifiers against that map using extension and
//     directory-index probing
//
// Bare/package imports are intentionally not resolved here.
struct JsModuleMap {
    std::unordered_map<std::string, uint32_t> path_to_id;

    static JsModuleMap build(const Index& idx);

    std::optional<uint32_t> resolve_relative(uint32_t file_id, std::string_view module_str,
                                             const Index& idx) const;
};

} // namespace rs
