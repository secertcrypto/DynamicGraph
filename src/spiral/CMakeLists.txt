cmake_minimum_required(VERSION 3.16)
project(spiral_u64 LANGUAGES C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# Compatibility for pinned upstream CMake files with CMake >= 4.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(HEXL_TESTING OFF CACHE BOOL "" FORCE)
set(HEXL_BENCHMARK OFF CACHE BOOL "" FORCE)
set(HEXL_DOCS OFF CACHE BOOL "" FORCE)
set(HEXL_TREAT_WARNING_AS_ERROR OFF CACHE BOOL "" FORCE)
set(SPIRAL_THIRD_PARTY "${CMAKE_CURRENT_LIST_DIR}/../../third_party" CACHE PATH "Local dependency source root")
if(NOT TARGET hexl)
  add_subdirectory("${SPIRAL_THIRD_PARTY}/hexl" "${CMAKE_CURRENT_BINARY_DIR}/hexl" EXCLUDE_FROM_ALL)
endif()
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)
add_library(spiral_u64 STATIC spiral_u64.cpp
  src/core.cpp src/poly.cpp src/util.cpp
  src/client.cpp src/constants.cpp src/conversion.cpp)
target_compile_features(spiral_u64 PUBLIC cxx_std_17)
target_include_directories(spiral_u64 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(spiral_u64 PRIVATE TEXP=8 TEXPRIGHT=56 TCONV=4 TGSW=8
  QPBITS=20 PVALUE=256 QNUMFIRST=1 QNUMREST=1 OUTN=2)
target_link_libraries(spiral_u64 PRIVATE hexl OpenSSL::Crypto Threads::Threads)
option(SPIRAL_U64_SANITIZERS "Check adapter memory and undefined behavior" OFF)
if(SPIRAL_U64_SANITIZERS)
  target_compile_options(spiral_u64 PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(spiral_u64 PUBLIC -fsanitize=address,undefined)
endif()

# Keep standalone builds using the same tests as the full project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../test/spiral"
      "${CMAKE_CURRENT_BINARY_DIR}/test/spiral")
  endif()
endif()
