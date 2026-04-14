#include "tokenizer.h"

#include <cctype>

namespace rs {

// Lowercase `text` in-place and return views of each alphanumeric run.
// A run of length < 2 is skipped — single-char tokens are noise.
std::vector<std::string_view> tokenize(std::string& text) {
    // Lowercase in a single pass (avoid locale overhead — ASCII only).
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }

    std::vector<std::string_view> tokens;
    tokens.reserve(text.size() / 5); // rough estimate: avg token length ~5

    const char* data = text.data();
    const std::size_t n = text.size();
    std::size_t i = 0;

    while (i < n) {
        // Skip non-alphanumeric
        while (i < n && !std::isalnum(static_cast<unsigned char>(data[i])))
            ++i;
        if (i >= n)
            break;

        const std::size_t start = i;

        // Consume alphanumeric run
        while (i < n && std::isalnum(static_cast<unsigned char>(data[i])))
            ++i;

        const std::size_t len = i - start;
        if (len >= 2) { // skip single-char tokens
            tokens.emplace_back(data + start, len);
        }
    }

    return tokens;
}

// Owned version: copies into a local buffer, lowercases, then extracts tokens
// as std::string.  Used on the query path where a small allocation is fine.
std::vector<std::string> tokenize_owned(std::string_view text) {
    // Copy + lowercase
    std::string buf(text);
    for (char& c : buf) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }

    std::vector<std::string> tokens;
    const char* data = buf.data();
    const std::size_t n = buf.size();
    std::size_t i = 0;

    while (i < n) {
        while (i < n && !std::isalnum(static_cast<unsigned char>(data[i])))
            ++i;
        if (i >= n)
            break;

        const std::size_t start = i;
        while (i < n && std::isalnum(static_cast<unsigned char>(data[i])))
            ++i;

        const std::size_t len = i - start;
        if (len >= 2) {
            tokens.emplace_back(data + start, len);
        }
    }

    return tokens;
}

} // namespace rs
