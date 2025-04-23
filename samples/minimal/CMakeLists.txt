add_executable(minimal sample.cpp 
                       main.cpp)

target_link_libraries(minimal
  PRIVATE orb::orbgui)

target_compile_definitions(minimal
  PRIVATE SAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/")
