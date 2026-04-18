#include "import_resolver.h"

#include "js_modules.h"
#include "python_modules.h"
#include "source_path.h"

#include <filesystem>
#include <utility>

namespace rs {

struct ImportResolver::Impl {
    PythonModuleMap python_modules;
    JsModuleMap js_modules;
    const Index* idx{nullptr};
};

ImportResolver::ImportResolver() = default;

ImportResolver::~ImportResolver() { delete impl_; }

ImportResolver::ImportResolver(ImportResolver&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

ImportResolver& ImportResolver::operator=(ImportResolver&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

ImportResolver ImportResolver::build(const Index& idx) {
    ImportResolver resolver;
    resolver.impl_ = new Impl{
        .python_modules = PythonModuleMap::build(idx),
        .js_modules = JsModuleMap::build(idx),
        .idx = &idx,
    };
    return resolver;
}

std::optional<uint32_t> ImportResolver::resolve(uint32_t file_id, std::string_view import_str) const {
    if (!impl_ || !impl_->idx || file_id >= impl_->idx->paths.size())
        return std::nullopt;

    const SourceLang lang =
        classify_source_path(std::filesystem::path(impl_->idx->paths[file_id]));

    if (lang == SourceLang::python)
        return impl_->python_modules.resolve_import(file_id, import_str);

    if (lang == SourceLang::js_ts && !import_str.empty() && import_str.front() == '.')
        return impl_->js_modules.resolve_relative(file_id, import_str, *impl_->idx);

    return std::nullopt;
}

} // namespace rs
