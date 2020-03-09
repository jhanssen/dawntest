cmake_minimum_required(VERSION 3.11)
include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES
    main.cpp
    )

add_executable(dt ${SOURCES})

target_link_libraries(dt glm::glm glfw ${GLFW_LIBRARIES} reckoning)
