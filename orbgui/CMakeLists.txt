add_library(orbgui STATIC src/orbgui.cpp)

add_library(orb::orbgui ALIAS orbgui)

target_include_directories(orbgui PUBLIC  include
                                  PRIVATE src)

target_link_libraries(orbgui PUBLIC orb::orbrenderer)
