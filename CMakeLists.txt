cmake_minimum_required(VERSION 3.25)
project(petrichor LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(petrichor STATIC
    src/loader.cpp
    src/platform.cpp
    src/backends/native_backend.cpp
)

target_include_directories(petrichor
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(vendor/luau)

file(GLOB PRELUDE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/backends/luau/lib/*.luau")

find_package(Python3 REQUIRED)

set(PRELUDE_HEADERS "")
foreach(SRC ${PRELUDE_SOURCES})
    get_filename_component(NAME ${SRC} NAME_WE)
    set(OUT ${CMAKE_CURRENT_BINARY_DIR}/prelude_${NAME}_inc.h)
    list(APPEND PRELUDE_HEADERS ${OUT})
endforeach()

add_custom_target(petrichor_preludes
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2h.py
            ${PRELUDE_SOURCES} ${CMAKE_CURRENT_BINARY_DIR} luau
    DEPENDS ${PRELUDE_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2h.py
)

add_dependencies(petrichor petrichor_preludes)
target_include_directories(petrichor PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_sources(petrichor PRIVATE
    src/backends/luau/host.cpp
    src/backends/luau/backend.cpp
    src/backends/luau/bindings/async.cpp
    src/backends/luau/bindings/net.cpp
)
target_link_libraries(petrichor PRIVATE Luau.VM Luau.Compiler)
target_include_directories(petrichor PRIVATE vendor/luau/VM/include vendor/luau/Compiler/include)

if(NOT WIN32)
    target_link_libraries(petrichor PRIVATE ${CMAKE_DL_LIBS})
endif()

option(PETRICHOR_TESTS "Build petrichor tests" OFF)
if(PETRICHOR_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
