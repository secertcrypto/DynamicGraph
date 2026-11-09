# Benchmarks for CRiS-MPC/3pc

find_package(OpenMP)

function(add_benchmark target_name)
  set(source_file "primitives/${target_name}.cpp")
  if(ARGN)
    list(GET ARGN 0 source_file)
  endif()

  add_executable(${target_name}
    ${source_file}
    utils.cpp
  )

  target_link_libraries(${target_name}
    PRIVATE
      emp-tool::emp-tool
      nlohmann_json::nlohmann_json
  )

  if(OpenMP_CXX_FOUND)
    target_link_libraries(${target_name}
      PRIVATE
        OpenMP::OpenMP_CXX
    )
  endif()

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}
  )
endfunction()

add_benchmark(bench_gate)
add_benchmark(bench_linear)
add_benchmark(bench_mult)
add_benchmark(bench_propagate)
add_benchmark(bench_permsh)
add_benchmark(bench_amor_permshare)
add_benchmark(bench_unshuffle)
add_library(graphiti_benchmark_common STATIC graphiti/common.cpp)
target_link_libraries(graphiti_benchmark_common PUBLIC
  Graphiti::graphiti emp-tool::emp-tool nlohmann_json::nlohmann_json)
if(OpenMP_CXX_FOUND)
  target_link_libraries(graphiti_benchmark_common PUBLIC OpenMP::OpenMP_CXX)
endif()
foreach(target bench_shuffle microbench_InitGraphiti bench_BfsMpaGraphiti bench_PrMpaGraphiti)
  add_benchmark(${target} graphiti/${target}.cpp)
  target_link_libraries(${target} PRIVATE graphiti_benchmark_common)
endforeach()
if(TARGET Graphiti::init2pc)
  add_benchmark(bench_FullGraphiti graphiti/bench_FullGraphiti.cpp)
  target_link_libraries(bench_FullGraphiti PRIVATE Graphiti::init2pc)
  add_benchmark(bench_InitGraphiti2pc graphiti/bench_InitGraphiti2pc.cpp)
  target_link_libraries(bench_InitGraphiti2pc PRIVATE graphiti_benchmark_common)
  target_compile_options(bench_InitGraphiti2pc PRIVATE -mpclmul)
endif()
add_benchmark(bench_PrMpaGraSP ${CMAKE_SOURCE_DIR}/benchmark/grasp/bench_PrMpaGraSP.cpp)
add_benchmark(RiskPropagation ${CMAKE_SOURCE_DIR}/benchmark/grasp/RiskPropagation.cpp)
add_benchmark(GroupConnection ${CMAKE_SOURCE_DIR}/benchmark/grasp/GroupConnection.cpp)
add_benchmark(bench_sort)

if(TARGET RISA::read)
  add_benchmark(bench_risa)
  target_link_libraries(bench_risa PRIVATE RISA::read RISA::update)
endif()

if(TARGET DynamicGraph::engine)
  add_executable(bench_dynamic_graph dynamic_graph/worker.cpp)
  target_link_libraries(bench_dynamic_graph PRIVATE
    DynamicGraph::engine nlohmann_json::nlohmann_json)
endif()
