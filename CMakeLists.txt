cmake_minimum_required(VERSION 3.25)

project(
    MemoryForge
    VERSION 0.1.0
    DESCRIPTION "A systems programming laboratory for memory management"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MEMORYFORGE_BUILD_TESTS "Build the test suite" ON)
option(MEMORYFORGE_BUILD_EXAMPLES "Build the example programs" ON)
option(MEMORYFORGE_ENABLE_SANITIZERS "Build with AddressSanitizer (and UBSan where supported)" OFF)
option(MEMORYFORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Single-config generators default to Release so that benchmarks are meaningful.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Interface target carrying warning and sanitizer flags for every MemoryForge target.
add_library(memoryforge_options INTERFACE)

if(MSVC)
    target_compile_options(memoryforge_options INTERFACE /W4 /permissive- /utf-8)
    if(MEMORYFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(memoryforge_options INTERFACE /WX)
    endif()
    if(MEMORYFORGE_ENABLE_SANITIZERS)
        target_compile_options(memoryforge_options INTERFACE /fsanitize=address)
    endif()
else()
    target_compile_options(
        memoryforge_options
        INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
    if(MEMORYFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(memoryforge_options INTERFACE -Werror)
    endif()
    if(MEMORYFORGE_ENABLE_SANITIZERS)
        target_compile_options(
            memoryforge_options
            INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
        )
        target_link_options(memoryforge_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# Record the git revision at configure time for report metadata.
set(MEMORYFORGE_GIT_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE MEMORYFORGE_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE MEMORYFORGE_GIT_RESULT
    )
    if(MEMORYFORGE_GIT_RESULT EQUAL 0 AND MEMORYFORGE_GIT_DESCRIBE)
        set(MEMORYFORGE_GIT_COMMIT "${MEMORYFORGE_GIT_DESCRIBE}")
    endif()
endif()

add_subdirectory(src)

if(MEMORYFORGE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(MEMORYFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
