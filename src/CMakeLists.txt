cmake_minimum_required(VERSION 3.11)
include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${DAWNTEST_CMAKE_DIR}/dawn.cmake)

set(SOURCES
    main.cpp
    render/Animation.cpp
    render/Utils.cpp
    )

if (APPLE)
    list(APPEND SOURCES render/backend/Backend_mt.mm)
else ()
    list(APPEND SOURCES render/backend/Backend_vk.cpp)
endif ()

add_executable(dt ${SOURCES})

target_link_libraries(dt glm::glm glfw ${GLFW_LIBRARIES} DAWN::libdawn_native DAWN::libdawn_wire DAWN::libdawn_proc DAWN::libshaderc DAWN::libshaderc_spvc DAWN::libdawn_cpp reckoning)

if (APPLE)
    target_link_libraries(dt "-framework Metal -framework QuartzCore")
endif ()
