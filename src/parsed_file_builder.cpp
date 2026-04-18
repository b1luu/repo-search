#include "parsed_file_builder.h"

#include "collection_utils.h"
#include "import_extractor.h"
#include "source_path.h"
#include "tokenizer.h"

#include <utility>

namespace rs {

ParsedFile build_parsed_file(const std::filesystem::path& path, std::string content) {
    ParsedFile pf;
    pf.path = path.string();
    pf.content = std::move(content);

    const SourceLang lang = classify_source_path(path);
    extract_imports(lang, pf.content, pf.imports);
    dedupe_preserve_order(pf.imports);
    pf.token_views = tokenize(pf.content);

    return pf;
}

} // namespace rs
