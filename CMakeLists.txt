cmake_minimum_required(VERSION 3.25)
project(petrichor LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(miniz STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniz/miniz.c
)

add_library(petrichor STATIC
    src/loader.cpp
    src/archive.cpp
    src/platform.cpp
)

target_include_directories(petrichor
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src 
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniz
)

add_subdirectory(vendor/luau)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_luau.cmake)
petrichor_embed_luau(petrichor ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/luau/lib)

target_sources(petrichor PRIVATE
    src/backends/luau/host.cpp
    src/backends/luau/backend.cpp

    src/backends/luau/bindings/async.cpp
    src/backends/luau/bindings/net.cpp
    src/backends/luau/bindings/storage.cpp
)
target_link_libraries(petrichor PRIVATE miniz Luau.VM Luau.Compiler)
target_include_directories(petrichor PRIVATE
    vendor/luau/VM/include
    vendor/luau/Compiler/include
)

if(NOT WIN32)
    target_link_libraries(petrichor PRIVATE ${CMAKE_DL_LIBS})
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(petrichor PRIVATE pthread m dl)
elseif(WIN32)
    target_link_libraries(petrichor PRIVATE ole32)
endif()

option(PETRICHOR_TESTS "Build petrichor tests" OFF)
if(PETRICHOR_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
