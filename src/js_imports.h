#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rs {

// Extract JS/TS module specifiers used by static imports, re-exports,
// require(...), and dynamic import("...") expressions.
void extract_js_imports(std::string_view content, std::vector<std::string>& out);

} // namespace rs
