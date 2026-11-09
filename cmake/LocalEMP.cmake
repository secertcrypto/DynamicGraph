# Build the in-tree EMP 0.2.5 sources without invoking its standalone CMake
# project (which assumes CMAKE_SOURCE_DIR points at EMP's root).
include_guard(GLOBAL)

set(EMP_TOOL_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/emp-tool"
    CACHE PATH "Local EMP 0.2.5 source checkout")

if(NOT EXISTS "${EMP_TOOL_SOURCE_DIR}/emp-tool/emp-tool.cpp")
  message(FATAL_ERROR
    "EMP sources not found at ${EMP_TOOL_SOURCE_DIR}. "
    "Run git submodule update --init --recursive from the repository root. "
    "For a source archive without submodules, clone emp-tool tag 0.2.5 into "
    "third_party/emp-tool first.")
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$"
   OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  message(FATAL_ERROR "Local EMP currently requires an x86 GNU/Clang toolchain (use WSL on Windows).")
endif()

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

# Match the default upstream 0.2.5 library source list. Optional
# CRYPTO_IN_CIRCUIT and THREADING builds are not enabled by this adapter.
set(_emp_sources "${EMP_TOOL_SOURCE_DIR}/emp-tool/emp-tool.cpp")
foreach(_operation add cos div eq le leq mul sin sq sqrt sub exp2 exp ln log2)
  list(APPEND _emp_sources
    "${EMP_TOOL_SOURCE_DIR}/emp-tool/circuits/float32_${_operation}.cpp")
endforeach()

add_library(cris_emp_tool STATIC ${_emp_sources})
add_library(emp-tool::emp-tool ALIAS cris_emp_tool)
set_target_properties(cris_emp_tool PROPERTIES
  OUTPUT_NAME emp-tool
  POSITION_INDEPENDENT_CODE ON)
target_include_directories(cris_emp_tool PUBLIC "${EMP_TOOL_SOURCE_DIR}")
target_compile_features(cris_emp_tool PUBLIC cxx_std_17)
target_compile_options(cris_emp_tool PUBLIC -maes -msse4.2 -msse2)
# EMP 0.2.5's block.h uses uint64_t without including <cstdint>. Supply it
# consistently to EMP and its header consumers without editing vendor files.
target_compile_options(cris_emp_tool PUBLIC -include cstdint)
target_link_libraries(cris_emp_tool PUBLIC OpenSSL::Crypto Threads::Threads)

message(STATUS "EMP: building local static library from ${EMP_TOOL_SOURCE_DIR}")
unset(_emp_sources)
unset(_operation)
