cmake_minimum_required(VERSION 3.11)
project(dawntest)

### Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if(EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(THIRDPARTY_DIR ${CMAKE_CURRENT_LIST_DIR}/3rdparty)
set(THIRDPARTY_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/3rdparty)

add_subdirectory(3rdparty ${CMAKE_BINARY_DIR}/3rdparty)
add_subdirectory(src ${CMAKE_BINARY_DIR}/src)
