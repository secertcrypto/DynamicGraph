add_executable(graphiti_module graphiti_module.cpp)
target_link_libraries(graphiti_module PRIVATE Graphiti::graphiti emp-tool::emp-tool)

if(TARGET Graphiti::init2pc)
  add_subdirectory(init2pc)
  set(full_graphiti_extra)
  if(TARGET bench_dynamic_graph)
    list(APPEND full_graphiti_extra --dynamic-binary $<TARGET_FILE:bench_dynamic_graph>)
  endif()
  add_test(NAME graphiti_full_benchmark COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/check_full.py
    --binary $<TARGET_FILE:bench_FullGraphiti> ${full_graphiti_extra})
  set_tests_properties(graphiti_full_benchmark PROPERTIES
    TIMEOUT 240 RESOURCE_LOCK graphiti_full_ports)
  add_test(NAME graphiti_rebuild_benchmark COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/check_rebuild.py
    --binary $<TARGET_FILE:bench_FullGraphiti> ${full_graphiti_extra})
  set_tests_properties(graphiti_rebuild_benchmark PROPERTIES
    TIMEOUT 240 RESOURCE_LOCK graphiti_rebuild_ports)
endif()

set(module_modes nph rss3 generic)
if(TARGET Graphiti::init2pc)
  list(APPEND module_modes two_party)
endif()
foreach(mode ${module_modes})
  set(backend ${mode})
  set(extra)
  if(mode STREQUAL "generic")
    set(backend nph)
    set(extra generic)
  endif()
  add_test(NAME graphiti_module_${mode}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/run_graphiti_module.py
            $<TARGET_FILE:graphiti_module> ${backend} 17300 ${extra})
  set_tests_properties(graphiti_module_${mode} PROPERTIES
    TIMEOUT 55 RESOURCE_LOCK graphiti_module_ports)
endforeach()

set(benchmark_modes nph rss3 generic nph3 pking)
if(TARGET Graphiti::init2pc)
  list(APPEND benchmark_modes two_party)
endif()
foreach(mode ${benchmark_modes})
  set(backend nph)
  set(init_target microbench_InitGraphiti)
  set(parties 2)
  set(extra)
  if(mode STREQUAL "two_party")
    set(backend two_party)
    set(init_target bench_InitGraphiti2pc)
  elseif(mode STREQUAL "rss3")
    set(backend rss3)
    set(parties 3)
  elseif(mode STREQUAL "generic")
    set(extra --generic)
  elseif(mode STREQUAL "nph3")
    set(parties 3)
  elseif(mode STREQUAL "pking")
    set(extra --pking)
  endif()
  add_test(NAME graphiti_benchmarks_${mode} COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/run_benchmarks.py
    --bfs $<TARGET_FILE:bench_BfsMpaGraphiti>
    --pagerank $<TARGET_FILE:bench_PrMpaGraphiti>
    --init $<TARGET_FILE:${init_target}>
    --shuffle $<TARGET_FILE:bench_shuffle>
    --protocol ${backend} --parties ${parties} ${extra})
  set_tests_properties(graphiti_benchmarks_${mode} PROPERTIES
    TIMEOUT 180 RESOURCE_LOCK graphiti_benchmark_ports)
endforeach()
