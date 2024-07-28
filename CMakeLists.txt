cmake_minimum_required(VERSION 3.5)

project(CoroIOMultiplex)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.hpp")
file(GLOB_RECURSE DEMO_SRC "demo/*.cpp")


add_library(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC include)

add_executable(demo ${DEMO_SRC})
target_link_libraries(demo ${PROJECT_NAME})
