cmake_minimum_required(VERSION 3.5)

project(CoroIOMultiplex)
# set(CMAKE_CXX_LINKER_LAUNCHER "ld.lld-18")
# set(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++")

# set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.hpp")
file(GLOB_RECURSE DEMO_SRC "demo/*.cpp")

include_directories(include)

add_compile_options(-std=c++26 -stdlib=libstdc++ -Wall -Wextra -Wpedantic -Werror)
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -g -fsanitize=address -fno-omit-frame-pointer")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -Ofast")
SET(CMAKE_CXX_LINKER_FLAGS_DEBUG "$ENV{LDFLAGS} -fsanitize=address")


add_library(${PROJECT_NAME} STATIC ${SOURCES})

find_package(TBB REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE TBB::tbb)

include_directories(${TBB_INCLUDE_DIRS})

foreach(_demo_src ${DEMO_SRC})
  string(REGEX MATCH [A-Za-z0-9\-\_]+.cpp _demo_target ${_demo_src}) # extract

  # filename
  string(LENGTH ${_demo_target} _demo_len)
  math(EXPR _demo_len "${_demo_len}-4")
  string(SUBSTRING ${_demo_target} 0 ${_demo_len} _demo_target) # extract filename
                                                             # without extension
  message("${_demo_src} -> ${_demo_target}")
  add_executable(${_demo_target} ${_demo_src}) # add target
  target_link_libraries(${_demo_target} PRIVATE ${PROJECT_NAME})
  target_link_libraries(${_demo_target} PRIVATE TBB::tbb)
endforeach(_demo_src ${DEMO_SRC})
  
