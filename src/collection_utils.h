#pragma once

#include <string>
#include <vector>

namespace rs {

// Deduplicate strings in-place while preserving first-occurrence order.
void dedupe_preserve_order(std::vector<std::string>& values);

} // namespace rs
