add_executable(graphiti_init2pc_run run.cpp)
target_link_libraries(graphiti_init2pc_run PRIVATE Graphiti::init2pc nlohmann_json::nlohmann_json)
add_executable(graphiti_init2pc_primitives test_primitives.cpp)
target_link_libraries(graphiti_init2pc_primitives PRIVATE Graphiti::init2pc)
target_compile_options(graphiti_init2pc_primitives PRIVATE -mpclmul)

add_test(NAME graphiti_init2pc_correctness COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/check.py --binary $<TARGET_FILE:graphiti_init2pc_run>)
add_test(NAME graphiti_init2pc_primitives COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/test/risa/run_two_party.py
  $<TARGET_FILE:graphiti_init2pc_primitives> --port 29810)
set_tests_properties(graphiti_init2pc_correctness graphiti_init2pc_primitives
  PROPERTIES TIMEOUT 180 RESOURCE_LOCK graphiti_init2pc_ports)

add_executable(graphiti_two_party_arithmetic test_arithmetic.cpp)
target_link_libraries(graphiti_two_party_arithmetic PRIVATE Graphiti::init2pc)
target_compile_options(graphiti_two_party_arithmetic PRIVATE -mpclmul)
add_test(NAME graphiti_two_party_arithmetic COMMAND ${Python3_EXECUTABLE}
  ${PROJECT_SOURCE_DIR}/test/risa/run_two_party.py
  $<TARGET_FILE:graphiti_two_party_arithmetic> --port 29820)
set_tests_properties(graphiti_two_party_arithmetic PROPERTIES TIMEOUT 180)
