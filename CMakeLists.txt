cmake_minimum_required(VERSION 3.5)

project(CoroIOMultiplex)
# set(CMAKE_CXX_LINKER_LAUNCHER "ld.lld-18")
# set(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# set(CMAKE_CXX_STANDARD 23)
#
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.hpp")
file(GLOB_RECURSE DEMO_SRC "demo/*.cpp")

include_directories(include)


add_compile_options(-std=c++26 -stdlib=libstdc++ -Wall -Wextra -Wpedantic -Werror)
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -Og -g -fsanitize=thread,undefined -fno-omit-frame-pointer -fPIE")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -Ofast")
SET(CMAKE_CXX_LINKER_FLAGS_DEBUG "$ENV{LDFLAGS} -fsanitize=thread,undefined -g -fPIE")
SET(CMAKE_CXX_LINKER_FLAGS_RELEASE "$ENV{LDFLAGS}")


add_library(${PROJECT_NAME} STATIC ${SOURCES})

find_package(TBB REQUIRED)
set(THIRD_PARTY_INCLUDE_DIRS ${TBB_INCLUDE_DIRS})
set(THIRD_PARTY_INCLUDE_DIRS "${THIRD_PARTY_INCLUDE_DIRS} third/expected/include")

target_link_libraries(${PROJECT_NAME} PRIVATE TBB::tbb)

include_directories(${THIRD_PARTY_INCLUDE_DIRS})

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

