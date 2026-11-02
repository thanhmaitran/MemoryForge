add_library(memoryforge_test_main STATIC test_main.cpp)
target_include_directories(memoryforge_test_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(memoryforge_test_main PUBLIC memoryforge_options)

function(memoryforge_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE memoryforge memoryforge_test_main)
    target_compile_definitions(${name} PRIVATE MEMORYFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

memoryforge_add_test(common_tests unit/common/common_tests.cpp)
memoryforge_add_test(allocator_tests unit/allocator/allocator_tests.cpp)
memoryforge_add_test(vm_tests unit/vm/vm_tests.cpp)
memoryforge_add_test(workload_tests unit/workload/workload_tests.cpp)
memoryforge_add_test(allocator_property_tests property/allocator_property_tests.cpp)
memoryforge_add_test(vm_property_tests property/vm_property_tests.cpp)
memoryforge_add_test(cli_integration_tests integration/cli_tests.cpp)

# End-to-end self check through the real executable.
add_test(
    NAME validate_all_configs
    COMMAND memoryforge_cli validate --all --configs ${PROJECT_SOURCE_DIR}/configs
)
