# Both CrIS-MPC and SFSS use the same local EMP target.
add_library(cris_sfssgen STATIC ../sfssgen/dpf.cpp)
target_include_directories(cris_sfssgen PUBLIC
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/sfssgen/vendor)
target_compile_options(cris_sfssgen PUBLIC -mpclmul)
target_link_libraries(cris_sfssgen PUBLIC emp-tool::emp-tool)
add_library(risa_update STATIC update.cpp)
add_library(RISA::update ALIAS risa_update)
target_link_libraries(risa_update PUBLIC cris_sfssgen)
add_library(dynamic_graph_delta STATIC ../dynamic_graph/delta_updates.cpp)
add_library(DynamicGraph::delta_updates ALIAS dynamic_graph_delta)
target_link_libraries(dynamic_graph_delta PUBLIC RISA::update)


option(CRIS_BUILD_RISA_READ "Build Spiral-backed RISA reads" ON)
if(NOT CRIS_BUILD_RISA_READ)
    return()
endif()
add_subdirectory(../spiral ${CMAKE_CURRENT_BINARY_DIR}/spiral)
add_library(risa_read STATIC read.cpp)
add_library(RISA::read ALIAS risa_read)
target_link_libraries(risa_read PUBLIC cris_sfssgen spiral_u64)
add_subdirectory(../dynamic_graph ${CMAKE_BINARY_DIR}/dynamic_graph)
