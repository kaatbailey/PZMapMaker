# Standalone build for the C1 §1.2 instanced-draw falsifier.
#
# Deliberately NOT wired into the top-level CMakeLists: this is a throwaway
# measurement tool, links no project code (no libpzformat), and should never
# become a build dependency of the editor. Build and run it on the dev machine,
# record the number in STATE.md, then it can sit here as the record of the test.
#
#   cmake -S harness -B harness/build -G Ninja
#   cmake --build harness/build
#   ./harness/build/instance_harness            # 500k instances, the C1 target
#
cmake_minimum_required(VERSION 3.25)
project(pz_instance_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)  # measure optimized
endif()

# QOpenGLFunctions_4_5_Core lives in Qt6::OpenGL; the offscreen context/surface
# come from Qt6::Gui. No Widgets needed — this renders to an FBO, no window.
find_package(Qt6 REQUIRED COMPONENTS Core Gui OpenGL)
find_package(OpenGL REQUIRED)

add_executable(instance_harness instance_harness.cpp)
target_link_libraries(instance_harness PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL OpenGL::GL)
target_compile_options(instance_harness PRIVATE -Wall -Wextra)
