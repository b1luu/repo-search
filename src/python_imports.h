#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rs {

// Extract Python import module strings while preserving full dotted paths and
// relative-import syntax for later resolution.
void extract_python_imports(std::string_view content, std::vector<std::string>& out);

} // namespace rs
