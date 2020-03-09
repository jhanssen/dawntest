cmake_minimum_required(VERSION 3.11)

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

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    execute_process(
        COMMAND gn gen ${THIRDPARTY_BINARY_DIR}/dawn/Debug "--args=is_debug = true"
        WORKING_DIRECTORY ${THIRDPARTY_DIR}/dawn
        )
    execute_process(
        COMMAND ninja -C ${THIRDPARTY_BINARY_DIR}/dawn/Debug
        WORKING_DIRECTORY ${THIRDPARTY_DIR}/dawn
        )
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    execute_process(
        COMMAND gn gen ${THIRDPARTY_BINARY_DIR}/dawn/Relase "--args=is_debug = false"
        WORKING_DIRECTORY ${THIRDPARTY_DIR}/dawn
        )
    execute_process(
        COMMAND ninja -C ${THIRDPARTY_BINARY_DIR}/dawn/Release
        WORKING_DIRECTORY ${THIRDPARTY_DIR}/dawn
        )
else ()
    message(FATAL_ERROR "Unknown build type ${CMAKE_BUILD_TYPE}")
endif ()

add_subdirectory(glfw)
add_subdirectory(glm)
add_subdirectory(reckoning)
