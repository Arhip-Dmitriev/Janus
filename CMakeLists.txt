cmake_minimum_required(VERSION 3.20)
project(janus VERSION 0.1.0 LANGUAGES C CXX)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Release build when no build type is specified.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Release, Debug, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# QuEST v3 via FetchContent.
#
# The QuEST repository ships v4 on its main branch with 
# backward-compatible v3 API access gated behind 
# ENABLE_DEPRECATED_API. This fetches QuEST, enables the
# deprecated API, and disable unneccessary features.

include(FetchContent)

set(FETCHCONTENT_QUIET ON)

# Disable QuEST features that are not required.
set(BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING       OFF CACHE BOOL "" FORCE)
set(ENABLE_CUDA          OFF CACHE BOOL "" FORCE)
set(ENABLE_CUQUANTUM     OFF CACHE BOOL "" FORCE)
set(ENABLE_HIP           OFF CACHE BOOL "" FORCE)
set(ENABLE_DISTRIBUTION  OFF CACHE BOOL "" FORCE)

# Enable the v3 deprecated API.
set(ENABLE_DEPRECATED_API ON CACHE BOOL "" FORCE)

# Double precision by default (FLOAT_PRECISION 2).
set(FLOAT_PRECISION 2 CACHE STRING "" FORCE)

FetchContent_Declare(
    quest
    GIT_REPOSITORY https://github.com/QuEST-Kit/QuEST.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(quest)


# Janus compiler sources.
#
# All implementation files live in Beta/src/.  CONFIGURE_DEPENDS causes
# the build system to re-glob when the directory contents change, which
# keeps the source list current as new files are added to the project.

file(GLOB JANUS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Beta/src/*.cpp"
)

# Janus executable.
add_executable(janus ${JANUS_SOURCES})

target_include_directories(janus PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Beta/src"
)

# Link against the QuEST library target produced by FetchContent.
target_link_libraries(janus PRIVATE QuEST)

# Ensures the math library is linked on platforms that require it.
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(janus PRIVATE ${MATH_LIBRARY})
endif()
