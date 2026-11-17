## == Luau (vendored) == ##
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(LUAU_WERROR OFF CACHE BOOL "" FORCE)
set(LUAU_EXTERN_C ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/luau)

## == miniz (vendored) == ##
add_library(miniz STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniz/miniz.c
)
target_include_directories(miniz PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniz
)

## == sol2 (vendored, Nerixyz Luau fork) == ##
# Wire vendored Luau into the Lua::Lua target sol2 expects
add_library(sol_luau INTERFACE)
add_library(Lua::Lua ALIAS sol_luau)
target_link_libraries(sol_luau INTERFACE Luau.VM Luau.Compiler)
target_include_directories(sol_luau INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/luau/VM/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/luau/Compiler/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/luau/Common/include
)

# Prevent sol2 from finding system Lua headers
set(SOL2_BUILD_LUA       FALSE CACHE BOOL "" FORCE)
set(SOL2_LUA_VERSION     "luau" CACHE STRING "" FORCE)
set(SOL2_TESTS           OFF CACHE BOOL "" FORCE)
set(SOL2_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(SOL2_ENABLE_INSTALL  OFF CACHE BOOL "" FORCE)

# Tell sol2 explicitly which Lua target to use
set(LUA_LIBRARIES Lua::Lua CACHE STRING "" FORCE)
set(LUA_INCLUDE_DIR 
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/luau/VM/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/luau/Common/include
    CACHE STRING "" FORCE)

# Luau has no lauxlib.h, and a bug in sol2 includes it whenever it is found on the host system
# An empty shim is used to fix this
set(_sol2_luau_shim_dir ${CMAKE_BINARY_DIR}/sol2_luau_shim)
file(WRITE ${_sol2_luau_shim_dir}/lauxlib.h "#pragma once\n")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/sol2)

target_compile_definitions(sol_luau INTERFACE SOL_LUAU=1 SOL_NO_LUA_HPP=1)
target_include_directories(sol_luau BEFORE INTERFACE ${_sol2_luau_shim_dir})
