add_executable(hook_runtime_test hook_runtime_test.cpp)
target_link_libraries(hook_runtime_test PRIVATE petrichor Luau.VM Luau.Compiler)
target_include_directories(hook_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/luau/VM/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/luau/Compiler/include
)
add_test(NAME hook_runtime COMMAND hook_runtime_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
