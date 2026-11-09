add_executable(dynamic_decode_test test_decode.cpp)
target_link_libraries(dynamic_decode_test PRIVATE DynamicGraph::engine)
add_test(NAME dynamic_secure_decode COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/test/run_graphiti_module.py $<TARGET_FILE:dynamic_decode_test> nph 19300)
add_test(NAME dynamic_graph_iterations COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/check_iterations.py $<TARGET_FILE:dynamic_graph_run>)
set_tests_properties(dynamic_secure_decode dynamic_graph_iterations PROPERTIES TIMEOUT 150)

add_test(NAME dynamic_graph_benchmark_smoke COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/benchmark/dynamic_graph/run.py --binary $<TARGET_FILE:bench_dynamic_graph>
  --vertices 5 --base-edges 4 --insert-schedule 0,2,0 --repeat 1 --port 20200 --check)
set_tests_properties(dynamic_graph_benchmark_smoke PROPERTIES TIMEOUT 150)

if(TARGET Graphiti::init2pc)
  add_test(NAME dynamic_graph_two_party_init COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/check_iterations.py $<TARGET_FILE:dynamic_graph_run>
    --two-party-init --port 20800)
  add_test(NAME dynamic_graph_two_party_benchmark_smoke COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/benchmark/dynamic_graph/run.py --binary $<TARGET_FILE:bench_dynamic_graph>
    --init-backend two_party --vertices 5 --base-edges 4 --insert-schedule 0,2,0
    --repeat 1 --port 21100 --check)
  set_tests_properties(dynamic_graph_two_party_init dynamic_graph_two_party_benchmark_smoke
    PROPERTIES TIMEOUT 150)
endif()

if(TARGET Graphiti::init2pc)
  add_test(NAME dynamic_two_party_decode COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/test/run_graphiti_module.py $<TARGET_FILE:dynamic_decode_test> two_party 19400)
  add_test(NAME dynamic_two_party_iterations COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/check_iterations.py $<TARGET_FILE:dynamic_graph_run>
    --backend two_party --port 21400)
  add_test(NAME dynamic_two_party_benchmark COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/benchmark/dynamic_graph/run.py --binary $<TARGET_FILE:bench_dynamic_graph>
    --backend two_party --vertices 5 --base-edges 4 --insert-schedule 0,2,0
    --repeat 2 --port 21700 --check)
  set_tests_properties(dynamic_two_party_decode dynamic_two_party_iterations dynamic_two_party_benchmark
    PROPERTIES TIMEOUT 180)
endif()
