cmake_minimum_required(VERSION 3.20)
project(repo_search CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy (Ninja/Makefile generators only).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Per-configuration compiler flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address,undefined")

# ---- Apple CLT workaround ---------------------------------------------------
# AppleClang via Command Line Tools (no full Xcode) may not resolve the libc++
# headers from its default include path.  Apply globally so every target —
# including the test executable — picks it up without per-target repetition.
if(APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE APPLE_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(EXISTS "${APPLE_SDK_PATH}/usr/include/c++/v1")
        add_compile_options(
            -nostdinc++
            -isystem${APPLE_SDK_PATH}/usr/include/c++/v1
            -isystem${APPLE_SDK_PATH}/usr/include
        )
        link_directories(${APPLE_SDK_PATH}/usr/lib)
    endif()
endif()

# ---- Warning flags (propagated via interface target) ------------------------
add_library(rs_warnings INTERFACE)
target_compile_options(rs_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    # Suppress during early development; re-enable later.
    -Wno-unused-parameter
)

# ---- Core library -----------------------------------------------------------
# OBJECT library: tokenizer + parser compiled once, linked into both the main
# binary and the test executable — no double-compilation, no separate archive.
add_library(rs_core OBJECT
    src/tokenizer.cpp
    src/parser.cpp
    src/collection_utils.cpp
    src/file_reader.cpp
    src/source_discovery.cpp
    src/python_imports.cpp
    src/js_imports.cpp
    src/import_extractor.cpp
    src/indexer.cpp
    src/python_modules.cpp
    src/js_modules.cpp
    src/import_resolver.cpp
    src/graph_builder.cpp
    src/search_engine.cpp
)
target_include_directories(rs_core PUBLIC src)
target_link_libraries(rs_core PUBLIC rs_warnings)

# ---- Main executable --------------------------------------------------------
add_executable(repo_search src/main.cpp)
target_link_libraries(repo_search PRIVATE rs_core)

# ---- Tests ------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
