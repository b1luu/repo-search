#pragma once

#include "source_path.h"

#include <string>
#include <string_view>
#include <vector>

namespace rs {

// Language-dispatch boundary for import extraction during parsing.
void extract_imports(SourceLang lang, std::string_view content, std::vector<std::string>& out);

} // namespace rs
