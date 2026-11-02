add_library(
    memoryforge STATIC
    allocator/free_list_allocator.cpp
    benchmark/allocator_benchmark.cpp
    benchmark/build_info.cpp
    benchmark/report.cpp
    benchmark/vm_benchmark.cpp
    cli/cli.cpp
    common/io.cpp
    common/json.cpp
    metrics/latency.cpp
    vm/replacement_policies.cpp
    vm/virtual_memory.cpp
    workload/allocation_trace.cpp
    workload/allocator_workload.cpp
    workload/page_trace.cpp
)
target_include_directories(memoryforge PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(memoryforge PUBLIC cxx_std_20)
target_link_libraries(memoryforge PUBLIC memoryforge_options)
target_compile_definitions(
    memoryforge
    PRIVATE
        MEMORYFORGE_VERSION="${PROJECT_VERSION}"
        MEMORYFORGE_GIT_COMMIT="${MEMORYFORGE_GIT_COMMIT}"
        MEMORYFORGE_BUILD_TYPE="$<CONFIG>"
)

add_executable(memoryforge_cli cli/main.cpp)
set_target_properties(memoryforge_cli PROPERTIES OUTPUT_NAME memoryforge)
target_link_libraries(memoryforge_cli PRIVATE memoryforge)
