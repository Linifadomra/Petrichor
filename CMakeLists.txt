cmake_minimum_required(VERSION 3.14)
project(petrichor
    VERSION 1.0.0
    DESCRIPTION "Petrichor runtime library"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake)

add_library(petrichor STATIC
    src/loader.cpp
    src/archive.cpp
    src/platform.cpp
    src/backends/luau/host.cpp
    src/backends/luau/backend.cpp
    src/backends/luau/bindings/async.cpp
    src/backends/luau/bindings/net.cpp
    src/backends/luau/bindings/storage.cpp
)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_luau.cmake)
petrichor_embed_luau(petrichor ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/luau/lib)

# Generate version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/petrichor_version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/petrichor/petrichor_version.hpp
    @ONLY
)

target_include_directories(petrichor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(petrichor
    PRIVATE
        miniz
    PUBLIC
        sol_luau
        Luau.VM
        Luau.Compiler
        sol2::sol2
)

if(NOT WIN32)
    target_link_libraries(petrichor PRIVATE ${CMAKE_DL_LIBS})
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(petrichor PRIVATE pthread m dl)
elseif(WIN32)
    target_link_libraries(petrichor PRIVATE ole32)
endif()

# ── Installation ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS petrichor
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/petrichor/petrichor_version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/petrichor
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/PetrichorConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/PetrichorConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/petrichor
)

# ── Tests ────────────────────────────────────────────────────────────────────

option(PETRICHOR_TESTS "Build petrichor tests" OFF)
if(PETRICHOR_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
