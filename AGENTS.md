# Agent Guide for cpp-russia-speech

This guide provides coding agents with essential information about building, testing, and contributing to this C++23 FFmpeg wrapper project.

## Quick Reference

**Working Directory**: All commands assume you're in `/mnt/source/repos/cpp-russia-speech/project/`

```bash
# Build
cmake -B out/build/x64-Debug -S . && cmake --build out/build/x64-Debug

# Run all tests
ctest --test-dir out/build/x64-Debug --output-on-failure

# Run single test (Catch2 filter)
out/build/x64-Debug/tests/ffmpeg_cpp/ffmpeg_cpp_tests.exe "Rational addition"

# Format code
clang-format -i <file>
```

## Project Overview

This is a modern C++23 project that wraps FFmpeg libraries using C++ modules (.ixx files). The project consists of:
- `ffmpeg_cpp`: Low-level C++ module wrappers for FFmpeg C APIs
- `mm_pipeline`: Higher-level multimedia pipeline abstractions (Demuxer, Muxer, Decoder)
- `app`: Demo application showing usage
- `tests`: Unit tests using Catch2 v3

## Build System

**CMake Version**: 3.31+ required
**C++ Standard**: C++23 with modules support
**Compiler**: MSVC 2022 (tested with 14.44.35207)
**Working Directory**: All commands below assume you're in the `project/` directory

### FFmpeg Setup

The project requires FFmpeg libraries to be available. FFmpeg location is specified via:

**Environment variable** (recommended):
```bash
export FFMPEG_ROOT=/path/to/ffmpeg-msvc  # Linux/Mac
set FFMPEG_ROOT=C:\sdk\ffmpeg-msvc       # Windows
```

**CMake variable** (alternative):
```bash
cmake -B out/build/x64-Debug -S . -DFFMPEG_ROOT=C:/sdk/ffmpeg-msvc
```

The FFmpeg directory must contain:
- `include/` - FFmpeg headers (libavutil, libavcodec, libavformat, etc.)
- `lib/` - Import libraries (.lib files)
- `bin/` - Runtime DLLs (avutil-59.dll, avcodec-61.dll, etc.)

**Configuration**: `project/cmake/FFmpegConfig.cmake` handles FFmpeg discovery and creates imported targets (FFmpeg::avutil, FFmpeg::avcodec, etc.)

### Build Commands

```bash
# Configure (from project directory)
cmake -B out/build/x64-Debug -S . -DCMAKE_BUILD_TYPE=Debug

# Build all targets
cmake --build out/build/x64-Debug

# Build specific target
cmake --build out/build/x64-Debug --target ffmpeg_demo
cmake --build out/build/x64-Debug --target ffmpeg_cpp_tests

# Format code with clang-format
clang-format -i <file>  # Uses project/.clang-format config
```

### Test Commands

```bash
# Run all tests
ctest --test-dir out/build/x64-Debug --output-on-failure

# Run tests with verbose output
ctest --test-dir out/build/x64-Debug -V

# Run specific test by name
ctest --test-dir out/build/x64-Debug -R "Rational addition" --verbose

# Run test executable directly
./out/build/x64-Debug/tests/ffmpeg_cpp/ffmpeg_cpp_tests.exe

# Run single test case with Catch2 filter
./out/build/x64-Debug/tests/ffmpeg_cpp/ffmpeg_cpp_tests.exe "Rational addition"

# List all test cases
./out/build/x64-Debug/tests/ffmpeg_cpp/ffmpeg_cpp_tests.exe --list-tests
```

## Code Style

### Formatting

**Tool**: clang-format (config: `project/.clang-format`)

**Key formatting rules**:
- **Indentation**: Tabs (4 spaces wide)
- **Braces**: Custom style - braces on new lines for classes, functions, namespaces, control statements
- **Line length**: No limit (ColumnLimit: 0)
- **Pointer alignment**: Left (`int* ptr`)
- **No column alignment** for assignments or declarations
- **Namespace comments**: Enabled (`// namespace foo`)

### Module Structure

**C++ Modules (.ixx files)**:
```cpp
module;

// Module purview preamble - includes go here
#include "../src/avutil.hpp"

export module ffmpeg.rational;

import std;  // Import standard library
import ffmpeg.error;  // Import other modules

namespace ffmpeg
{

export class Rational  // Export public API
{
	// Implementation
};

} // namespace ffmpeg
```

**Implementation files (.cpp)**:
```cpp
module;

#include "error.hpp"  // Private includes

module ffmpeg.error;  // Module implementation unit

namespace ffmpeg
{
// Implementation
} // namespace ffmpeg
```

### Naming Conventions

- **Classes/Structs**: PascalCase (`Rational`, `InputFormatContext`, `StreamInfo`)
- **Functions/Methods**: PascalCase (`GetStreamCount()`, `TryRead()`, `ToAV()`)
- **Variables**: camelCase for locals (`streamInfo`), `m_` prefix for members (`m_ctx`, `m_value`)
- **Constants**: kPascalCase (`kRationalTestTag`)
- **Namespaces**: lowercase (`ffmpeg`, `mm_pipeline`)
- **Enums**: PascalCase for type, PascalCase for values (`enum class ErrDomain : std::uint8_t`)

### Types and Const Correctness

- Use `[[nodiscard]]` for functions returning values that shouldn't be ignored
- Use `noexcept` where appropriate (constructors, getters, simple operations)
- Mark member functions `const` when they don't modify state
- Use `constexpr` for compile-time evaluable functions
- Prefer `const` references for parameters: `const Rational& a`
- Use explicit `explicit` constructors for single-argument constructors

### Error Handling

The project uses a custom error handling system with two approaches:

**1. Exception-based** (for operations that should not fail):
```cpp
void CheckFFmpegError(int code, const char* where);  // Throws on error
void ThrowFFmpegError(int code, const char* where);  // Always throws
```

**2. std::expected-based** (for operations where errors are expected):
```cpp
[[nodiscard]] std::expected<DemuxOutput, Error> TryRead();
[[nodiscard]] std::expected<void, Error> ExpectedFromFFmpegErrorCode(int code, const char* where);
```

**Error struct** is lightweight (≤16 bytes, trivially copyable):
```cpp
struct Error {
	int code = 0;
	ErrDomain domain = ErrDomain::None;
	const char* where = nullptr;
};
```

### Imports and Includes

**Order of imports/includes in modules**:
1. Module preamble (`module;`)
2. C library includes wrapped in `extern "C"` (private headers like `avutil.hpp`)
3. Module declaration (`export module name;`)
4. `import std;` (if needed)
5. Other module imports (`import ffmpeg.error;`)

**Private header pattern** for FFmpeg C APIs:
```cpp
// In avutil.hpp
#pragma once
extern "C" {
#include <libavutil/avutil.h>
}
```

### Modern C++ Features

This project heavily uses C++23 features:
- **Modules**: Primary code organization mechanism
- **std::expected**: For error handling
- **Concepts**: For template constraints (`requires` clauses)
- **Three-way comparison**: `operator<=>`
- **Designated initializers**: `.field = value`
- **[[likely]]/[[unlikely]]**: Branch hints

## Compiler Flags

**MSVC**:
- `/W4`: Warning level 4
- `/WX`: Treat warnings as errors

**GCC/Clang**:
- `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow`

**FFmpeg-specific defines** (already set in CMake):
```cpp
__STDC_CONSTANT_MACROS
__STDC_FORMAT_MACROS
__STDC_LIMIT_MACROS
```

## Testing

**Framework**: Catch2 v3 (fetched automatically via CMake FetchContent)

**Test file structure**:
```cpp
import ffmpeg.rational;  // Import module to test
#include <catch2/catch_test_macros.hpp>

namespace {
constexpr auto kRationalTestTag = "[rational]";
}

TEST_CASE("Test description", kRationalTestTag) {
	// Arrange
	const auto r = Rational{ 1, 3 } + Rational{ 1, 6 };
	
	// Assert
	REQUIRE(r.Num() == 1);
	REQUIRE(r.Den() == 2);
}
```

## Common Patterns

**RAII wrappers**: Use `unique_handle` template for FFmpeg C resources
**Move semantics**: Classes managing resources should be movable but not copyable
**Factory functions**: Prefer returning `std::expected<T, Error>` for operations that may fail
**Structured bindings**: Use for returning multiple values via structs

## Important Notes

- All FFmpeg API calls must include error checking
- Use `const char*` for string literals (FFmpeg C API convention)
- Windows paths in code use raw strings: `R"(C:\videos\file.mp4)"`
- DLL copying is automated via CMake `copy_runtime_dlls()` function
- Test discovery uses `catch_discover_tests()` with FFmpeg bin path for DLL loading
- **FFmpeg location**: Use `FFMPEG_ROOT` environment variable to specify FFmpeg installation path
