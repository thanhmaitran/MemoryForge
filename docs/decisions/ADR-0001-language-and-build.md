# ADR-0001: C++20, CMake and no third-party dependencies

* Status: accepted

## Context

The project exists to demonstrate systems programming: resource management, low-level data structures, API design and
reproducible measurement. It must build from a clean clone on Windows (MSVC, MinGW-w64), Linux and macOS.

## Decision

* **C++20** for RAII, `std::span`, concepts and `constexpr` utilities, with `CMAKE_CXX_EXTENSIONS OFF`.
* **CMake >= 3.25** with presets, so one command line works in VS Code, Visual Studio and CI.
* **No third-party libraries.** JSON parsing/writing and the test framework are small, self-contained and covered by
  tests. A clean clone needs only a compiler and CMake.
* **Errors are values** (`Result<T>`, `Status`) for expected failures; exceptions only signal programmer error.
* Warnings are part of the build (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`, `/W4 /permissive-`), and
  `MEMORYFORGE_WARNINGS_AS_ERRORS` turns them into errors in CI where the toolchain has been verified.

## Consequences

* More code to maintain than with nlohmann/json and GoogleTest, in exchange for zero dependency friction and full
  control of determinism.
* Python is used only for post-processing exported CSV files, never for measurement.
