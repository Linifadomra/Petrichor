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

target_sources(petrichor PRIVATE
    src/luau_host.cpp
    src/backends/luau_backend.cpp
)
target_link_libraries(petrichor PRIVATE Luau.VM Luau.Compiler)
target_include_directories(petrichor PRIVATE vendor/luau/VM/include vendor/luau/Compiler/include)

if(NOT WIN32)
    target_link_libraries(petrichor PRIVATE ${CMAKE_DL_LIBS})
endif()

# Managed projects (Petrichor.ModHost / Petrichor.ModSdk) for the consumer to publish + deploy.
set(PETRICHOR_MODHOST_CSPROJ ${CMAKE_CURRENT_SOURCE_DIR}/managed/Petrichor.ModHost/Petrichor.ModHost.csproj CACHE INTERNAL "")
set(PETRICHOR_MODSDK_CSPROJ  ${CMAKE_CURRENT_SOURCE_DIR}/managed/Petrichor.ModSdk/Petrichor.ModSdk.csproj   CACHE INTERNAL "")

option(PETRICHOR_TESTS "Build petrichor tests" OFF)
if(PETRICHOR_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
