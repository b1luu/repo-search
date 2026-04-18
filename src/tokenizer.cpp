#include "tokenizer.h"

#include <cctype>

namespace rs {

namespace {

// ASCII-only lowercase in-place. Explicit branch-on-range avoids locale
// overhead from std::tolower.
void lowercase_ascii_inplace(std::string& s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
}

// Walk a character buffer and invoke `emit(ptr, len)` for each alphanumeric
// run of length >= 2.  Shared by both tokenize() and tokenize_owned() so the
// scanning rules stay in one place.
template <typename Emit>
void scan_alnum_runs(const char* data, std::size_t n, Emit&& emit) {
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
        if (len >= 2) // skip single-char tokens (noise)
            emit(data + start, len);
    }
}

} // namespace

std::vector<std::string_view> tokenize(std::string& text) {
    lowercase_ascii_inplace(text);

    std::vector<std::string_view> tokens;
    tokens.reserve(text.size() / 5); // rough estimate: avg token length ~5

    scan_alnum_runs(text.data(), text.size(),
                    [&](const char* p, std::size_t len) { tokens.emplace_back(p, len); });

    return tokens;
}

std::vector<std::string> tokenize_owned(std::string_view text) {
    std::string buf(text);
    lowercase_ascii_inplace(buf);

    std::vector<std::string> tokens;
    scan_alnum_runs(buf.data(), buf.size(),
                    [&](const char* p, std::size_t len) { tokens.emplace_back(p, len); });

    return tokens;
}

} // namespace rs
