cmake_minimum_required(VERSION 3.20)
project(repo_search CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release by default; override with -DCMAKE_BUILD_TYPE=Debug
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address,undefined")

add_executable(repo_search
  src/main.cpp
  src/tokenizer.cpp
  src/parser.cpp
)

target_include_directories(repo_search PRIVATE src)

# Warn aggressively
target_compile_options(repo_search PRIVATE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion
)

# AppleClang (Command Line Tools only, no full Xcode) can fail to find the
# C++ standard library headers in its default search path.  Work around it by
# pointing explicitly at the SDK's libc++ headers when they exist.
if(APPLE)
  execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE APPLE_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(LIBCXX_INC "${APPLE_SDK_PATH}/usr/include/c++/v1")
  if(EXISTS "${LIBCXX_INC}")
    target_compile_options(repo_search PRIVATE
      -nostdinc++
      -isystem${LIBCXX_INC}
      -isystem${APPLE_SDK_PATH}/usr/include
    )
    target_link_options(repo_search PRIVATE -L${APPLE_SDK_PATH}/usr/lib)
  endif()
endif()
