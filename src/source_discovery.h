#pragma once

#include <filesystem>
#include <vector>

namespace rs {

// Recursively collect supported source files under `root`, normalize the
// paths, and return them in a deterministic sorted order.
std::vector<std::filesystem::path> discover_source_files(const std::filesystem::path& root);

} // namespace rs
