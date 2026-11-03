cmake_minimum_required(VERSION 3.16)

# Release unless asked otherwise. This must come before project(): on
# Windows, CMake's clang support picks Debug during project() when no
# build type is set, and an unoptimized ray tracer is painfully slow.
# Multi-config generators like Visual Studio ignore it and pick the
# configuration at build time (cmake --build build --config Release).
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

project(gauchoRT CXX)

# The scene files and the renderer are C++17, nothing more: no GNU
# extensions, so the same code builds on gcc, clang and MSVC.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The tile pool runs on std::thread.
find_package(Threads REQUIRED)

# Your renderer: every .cpp under src/, found at configure time and
# rechecked on each build, so a file you add is picked up by a rebuild.
# main.cpp, cmdline.cpp, transform.cpp, image.cpp, noise.cpp and debug.cpp are given
# complete; rt.cpp and shaders.cpp are the stubs you fill in.
file(GLOB RENDERER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(SORT RENDERER_SOURCES)

# Every .cpp under scenes/ (each homework's folder with its table), found
# the same way, so a scene of your own, or a later homework's folder
# dropped into the tree, is picked up by a rebuild.
file(GLOB_RECURSE SCENE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scenes/*.cpp")
list(SORT SCENE_SOURCES)

# Which scene tables exist besides HW2's (scenes/1_Whitted/):
# RT_HAVE_SCENES_HW3 for scenes/2_Cook/, _HW4 for scenes/3_Kajiya/ and
# _HIDDEN for scenes/hidden/, for cmdline.cpp (include/scenes.h). A new folder
# changes the glob above, which reruns this configure step, so the define
# follows.
set(SCENE_TABLE_DEFINES)
foreach(pair "2_Cook:HW3" "3_Kajiya:HW4" "hidden:HIDDEN")
  string(REPLACE ":" ";" pair "${pair}")
  list(GET pair 0 folder)
  list(GET pair 1 tag)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scenes/${folder}/0_scene_table.cpp")
    list(APPEND SCENE_TABLE_DEFINES "RT_HAVE_SCENES_${tag}")
  endif()
endforeach()

add_executable(gauchoRT ${RENDERER_SOURCES} ${SCENE_SOURCES})
target_include_directories(gauchoRT PRIVATE include src)
target_include_directories(gauchoRT SYSTEM PRIVATE third_party) # no warnings from stb
target_link_libraries(gauchoRT PRIVATE Threads::Threads)
target_compile_definitions(gauchoRT PRIVATE ${SCENE_TABLE_DEFINES})

if(MSVC)
  target_compile_options(gauchoRT PRIVATE /W4)
else()
  target_compile_options(gauchoRT PRIVATE -Wall -Wextra)
endif()
# fopen and snprintf are fine: silences Microsoft's C runtime, which clang
# on Windows uses too. Harmless elsewhere.
target_compile_definitions(gauchoRT PRIVATE _CRT_SECURE_NO_WARNINGS)

# The scene files are inputs, not code to fix, so do not warn about one
# thing they do on purpose: partially initialized parameter structs.
if(NOT MSVC)
  set_source_files_properties(${SCENE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wno-missing-field-initializers")
endif()
