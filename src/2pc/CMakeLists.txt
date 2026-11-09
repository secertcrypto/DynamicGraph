add_library(graphiti_init2pc STATIC init.cpp protocol.cpp evaluator.cpp libsss/benes.cpp)
add_library(Graphiti::init2pc ALIAS graphiti_init2pc)
target_include_directories(graphiti_init2pc PUBLIC
    ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/sfssgen/vendor)
target_link_libraries(graphiti_init2pc PUBLIC emp-tool::emp-tool)
target_compile_options(graphiti_init2pc PRIVATE -mpclmul)
