#include "import_extractor.h"

#include "js_imports.h"
#include "python_imports.h"

namespace rs {

void extract_imports(SourceLang lang, std::string_view content, std::vector<std::string>& out) {
    if (lang == SourceLang::python) {
        extract_python_imports(content, out);
    } else if (lang == SourceLang::js_ts) {
        extract_js_imports(content, out);
    }
}

} // namespace rs
