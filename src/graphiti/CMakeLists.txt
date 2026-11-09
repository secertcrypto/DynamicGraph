add_library(graphiti INTERFACE)
add_library(Graphiti::graphiti ALIAS graphiti)
target_include_directories(graphiti INTERFACE ${PROJECT_SOURCE_DIR})
target_compile_features(graphiti INTERFACE cxx_std_17)

if(CRIS_BUILD_INIT2PC)
  target_link_libraries(graphiti INTERFACE Graphiti::init2pc)
  target_compile_definitions(graphiti INTERFACE CRIS_HAS_2PC=1)
endif()
