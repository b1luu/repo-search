#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rs {

// Tokenize `text` in-place: lowercase, split on any non-alphanumeric character.
// Returns tokens as string_views into `text` — caller must keep `text` alive.
// For hot paths (indexing), call this and intern via Vocabulary.
std::vector<std::string_view> tokenize(std::string& text);

// Convenience overload that owns its own lowercase copy.
// Use for query tokenization (cold path); allocation is acceptable there.
std::vector<std::string> tokenize_owned(std::string_view text);

} // namespace rs
