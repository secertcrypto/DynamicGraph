add_executable(risa_update_example example_update.cpp)
target_link_libraries(risa_update_example PRIVATE DynamicGraph::delta_updates)
add_executable(risa_graphiti_example graphiti_batch_example.cpp)
target_link_libraries(risa_graphiti_example PRIVATE DynamicGraph::delta_updates Graphiti::graphiti)

foreach(target risa_update_test risa_batch_test)
  add_executable(${target} ${target}.cpp)
  target_link_libraries(${target} PRIVATE DynamicGraph::delta_updates)
  add_test(NAME ${target} COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/run_two_party.py $<TARGET_FILE:${target}> --port 28465)
  set_tests_properties(${target} PROPERTIES TIMEOUT 120 RESOURCE_LOCK cris_risa_port)
endforeach()

add_test(NAME risa_graphiti_nph COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/test/run_graphiti_module.py
  $<TARGET_FILE:risa_graphiti_example> nph 18500)
set_tests_properties(risa_graphiti_nph PROPERTIES TIMEOUT 55 RESOURCE_LOCK cris_nph_delta_port)

if(TARGET RISA::read)
  add_test(NAME risa_primitive_benchmark COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/check_benchmark.py --binary $<TARGET_FILE:bench_risa>)
  set_tests_properties(risa_primitive_benchmark PROPERTIES
    TIMEOUT 300 RESOURCE_LOCK risa_benchmark_ports)
  add_executable(risa_read_example read_example.cpp)
  target_link_libraries(risa_read_example PRIVATE RISA::read)
  add_executable(risa_graphiti_read_example graphiti_read_example.cpp)
  target_link_libraries(risa_graphiti_read_example PRIVATE
    RISA::read DynamicGraph::delta_updates Graphiti::graphiti)
  add_executable(risa_read_test risa_read_test.cpp)
  target_link_libraries(risa_read_test PRIVATE RISA::read)
  add_test(NAME risa_read_two_party COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/run_two_party.py $<TARGET_FILE:risa_read_test> --port 28466)
  add_test(NAME risa_read_graphiti_nph COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/test/run_graphiti_module.py
    $<TARGET_FILE:risa_graphiti_read_example> nph 18600)
  set_tests_properties(risa_read_two_party risa_read_graphiti_nph PROPERTIES TIMEOUT 120)
endif()
