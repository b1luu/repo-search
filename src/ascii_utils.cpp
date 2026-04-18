#include "ascii_utils.h"

namespace rs {

void ascii_lowercase_inplace(std::string& s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
}

std::string ascii_lowercase_copy(std::string_view text) {
    std::string out(text);
    ascii_lowercase_inplace(out);
    return out;
}

} // namespace rs
