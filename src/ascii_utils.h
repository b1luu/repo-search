#pragma once

#include <string>
#include <string_view>

namespace rs {

// ASCII-only lowercase in-place. Explicit branch-on-range avoids locale
// overhead from std::tolower and keeps the hot path allocation-free.
void ascii_lowercase_inplace(std::string& s);

// Convenience wrapper: return a lowercased copy of `text`.
std::string ascii_lowercase_copy(std::string_view text);

} // namespace rs
