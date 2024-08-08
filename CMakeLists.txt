cmake_minimum_required(VERSION 3.5)

project(CoroIOMultiplex)
# set(CMAKE_CXX_LINKER_LAUNCHER "ld.lld-18")
# set(CMAKE_EXE_LINKER_FLAGS "-v")

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.hpp")
file(GLOB_RECURSE DEMO_SRC "demo/*.cpp")

include_directories(include)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -g -fsanitize=address -fno-omit-frame-pointer")
add_link_options(-fsanitize=address)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

add_executable(demo ${DEMO_SRC})
target_link_libraries(demo PRIVATE ${PROJECT_NAME})
