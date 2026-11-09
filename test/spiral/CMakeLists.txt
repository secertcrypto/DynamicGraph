add_executable(spiral_u64_test test_spiral_u64.cpp)
target_link_libraries(spiral_u64_test PRIVATE spiral_u64)
add_test(NAME spiral_u64_correctness COMMAND spiral_u64_test)

if(SPIRAL_U64_SANITIZERS)
  target_compile_options(spiral_u64_test PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
