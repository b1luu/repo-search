#pragma once

#include "indexer.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace rs {

// Unified intra-corpus import resolver.
//
// This is the boundary between graph construction and language-specific
// module-resolution policy. Callers provide a file_id plus a raw import
// string and receive the target file_id when the import resolves within the
// indexed corpus.
class ImportResolver {
  public:
    static ImportResolver build(const Index& idx);

    std::optional<uint32_t> resolve(uint32_t file_id, std::string_view import_str) const;

  private:
    struct Impl;
    Impl* impl_{nullptr};

  public:
    ImportResolver();
    ~ImportResolver();
    ImportResolver(ImportResolver&& other) noexcept;
    ImportResolver& operator=(ImportResolver&& other) noexcept;

    ImportResolver(const ImportResolver&) = delete;
    ImportResolver& operator=(const ImportResolver&) = delete;
};

} // namespace rs
