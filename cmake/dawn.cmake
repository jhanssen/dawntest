if (NOT EXISTS "${THIRDPARTY_DIR}/dawn/.gclient")
    execute_process(
        COMMAND ./sync-dawn.sh
        WORKING_DIRECTORY ${THIRDPARTY_DIR}
        RESULT_VARIABLE SYNC_RESULT
        )
    if (NOT ${SYNC_RESULT} EQUAL 0)
        message(FATAL_ERROR "dawn sync failed")
    endif()
endif()

set(DAWN_SOURCE_DIR ${THIRDPARTY_DIR}/dawn)

# configure
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DAWN_BINARY_DIR ${THIRDPARTY_BINARY_DIR}/dawn/Debug)
    if (NOT EXISTS ${DAWN_BINARY_DIR}/build.ninja)
        execute_process(
            COMMAND gn gen ${DAWN_BINARY_DIR} "--args=is_debug = true"
            WORKING_DIRECTORY ${DAWN_SOURCE_DIR}
            )
    endif()
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(DAWN_BINARY_DIR ${THIRDPARTY_BINARY_DIR}/dawn/Release)
    if (NOT EXISTS ${DAWN_BINARY_DIR}/build.ninja)
        execute_process(
            COMMAND gn gen ${DAWN_BINARY_DIR} "--args=is_debug = false"
            WORKING_DIRECTORY ${DAWN_SOURCE_DIR}
            )
    endif()
else ()
    message(FATAL_ERROR "Unknown build type ${CMAKE_BUILD_TYPE}")
endif ()

# build
execute_process(
    COMMAND ninja -C ${DAWN_BINARY_DIR}
    WORKING_DIRECTORY ${DAWN_SOURCE_DIR}
    )

# make target
find_library(LIBDAWN_NATIVE dawn_native PATHS ${DAWN_BINARY_DIR}
    NO_DEFAULT_PATH NO_CMAKE_PATH NO_CMAKE_SYSTEM_PATH)
find_library(LIBDAWN_WIRE dawn_wire PATHS ${DAWN_BINARY_DIR}
    NO_DEFAULT_PATH NO_CMAKE_PATH NO_CMAKE_SYSTEM_PATH)
find_library(LIBDAWN_PROC dawn_proc PATHS ${DAWN_BINARY_DIR}
    NO_DEFAULT_PATH NO_CMAKE_PATH NO_CMAKE_SYSTEM_PATH)
find_library(LIBSHADERC shaderc PATHS ${DAWN_BINARY_DIR}
    NO_DEFAULT_PATH NO_CMAKE_PATH NO_CMAKE_SYSTEM_PATH)
find_library(LIBSHADERC_SPVC shaderc_spvc PATHS ${DAWN_BINARY_DIR}
    NO_DEFAULT_PATH NO_CMAKE_PATH NO_CMAKE_SYSTEM_PATH)

message("-- Using dawn from ${LIBDAWN_NATIVE}")

add_library(DAWN::libdawn_native UNKNOWN IMPORTED)
set_target_properties(DAWN::libdawn_native PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${DAWN_BINARY_DIR}/gen/src/include;${DAWN_SOURCE_DIR}/src/include")
set_target_properties(DAWN::libdawn_native PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
    IMPORTED_LOCATION ${LIBDAWN_NATIVE})

add_library(DAWN::libdawn_wire UNKNOWN IMPORTED)
set_target_properties(DAWN::libdawn_wire PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
    IMPORTED_LOCATION ${LIBDAWN_WIRE})

add_library(DAWN::libdawn_proc UNKNOWN IMPORTED)
set_target_properties(DAWN::libdawn_proc PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
    IMPORTED_LOCATION ${LIBDAWN_PROC})

add_library(DAWN::libshaderc UNKNOWN IMPORTED)
set_target_properties(DAWN::libshaderc PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
    IMPORTED_LOCATION ${LIBSHADERC})

add_library(DAWN::libshaderc_spvc UNKNOWN IMPORTED)
set_target_properties(DAWN::libshaderc_spvc PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
    IMPORTED_LOCATION ${LIBSHADERC_SPVC})
