cmake_minimum_required(VERSION 3.25)
project(petrichor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PETRICHOR_DOTNET_HOST_DIR "" CACHE PATH "Directory containing nethost.h / hostfxr.h / libnethost")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PetrichorBindgen.cmake)

add_library(petrichor STATIC
    src/loader.cpp
    src/clr_host.cpp
    src/platform.cpp
    src/backends/clr_backend.cpp
    src/backends/native_backend.cpp
)

target_include_directories(petrichor
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(PETRICHOR_DOTNET_HOST_DIR)
    target_include_directories(petrichor PRIVATE ${PETRICHOR_DOTNET_HOST_DIR})
    find_library(PETRICHOR_NETHOST nethost PATHS ${PETRICHOR_DOTNET_HOST_DIR} NO_DEFAULT_PATH)
    if(PETRICHOR_NETHOST)
        target_link_libraries(petrichor PRIVATE ${PETRICHOR_NETHOST})
    endif()
endif()

if(NOT WIN32)
    target_link_libraries(petrichor PRIVATE ${CMAKE_DL_LIBS})
endif()

# Managed projects (Petrichor.ModHost / Petrichor.ModSdk) for the consumer to publish + deploy.
set(PETRICHOR_MODHOST_CSPROJ ${CMAKE_CURRENT_SOURCE_DIR}/managed/Petrichor.ModHost/Petrichor.ModHost.csproj CACHE INTERNAL "")
set(PETRICHOR_MODSDK_CSPROJ  ${CMAKE_CURRENT_SOURCE_DIR}/managed/Petrichor.ModSdk/Petrichor.ModSdk.csproj   CACHE INTERNAL "")
