cmake_minimum_required(VERSION 3.14)
project(SS_PSI CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the editable in-tree EMP checkout; no system EMP installation needed.
# Existing consumers continue linking to emp-tool::emp-tool.
include(cmake/LocalEMP.cmake)

# ── PSI include root (so code can #include "src/3pc/...") ─────────────────────
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ── nlohmann/json (used by benchmark/utils.h) ────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
FetchContent_MakeAvailable(nlohmann_json)

option(CRIS_BUILD_INIT2PC "Build two-party secure computation and Graphiti initialization" ON)
if(CRIS_BUILD_INIT2PC)
  add_subdirectory(src/2pc)
endif()

add_subdirectory(src/3pc)
add_subdirectory(src/graphiti)
include(CTest)
add_subdirectory(src/risa)
add_subdirectory(benchmark)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()
