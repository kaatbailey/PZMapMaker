cmake_minimum_required(VERSION 3.25)
project(pzformat LANGUAGES CXX)

# Library layer: C++20, no dependencies beyond the standard library.
# (Charter §3, as amended for the C++ port.)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_library(pzformat STATIC
    le.cpp
    lew.cpp
    lotheader.cpp
    lotpack.cpp
    mapped_file.cpp
    tiledefs.cpp
    tilebin.cpp
    packfile.cpp
    spritenames.cpp
    celldata.cpp
)

target_include_directories(pzformat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_options(pzformat PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast
)

enable_testing()

function(pz_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pzformat)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

pz_add_test(pz_tests_le_lew    test_le_lew.cpp)
pz_add_test(pz_tests_lotheader test_lotheader.cpp)
pz_add_test(pz_tests_lotpack   test_lotpack.cpp)
pz_add_test(pz_tests_tiles     test_tiledefs_tilebin.cpp)
pz_add_test(pz_tests_packfile  test_packfile.cpp)
pz_add_test(pz_tests_celldata  test_celldata.cpp)

add_executable(pz_roundtrip roundtrip.cpp)
target_link_libraries(pz_roundtrip PRIVATE pzformat)
target_compile_options(pz_roundtrip PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pz_oracle oracle.cpp)
target_link_libraries(pz_oracle PRIVATE pzformat)
target_compile_options(pz_oracle PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pz_tile_oracle tile_oracle.cpp)
target_link_libraries(pz_tile_oracle PRIVATE pzformat)
target_compile_options(pz_tile_oracle PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pz_pack_oracle pack_oracle.cpp)
target_link_libraries(pz_pack_oracle PRIVATE pzformat)
target_compile_options(pz_pack_oracle PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pz_cell_oracle cell_oracle.cpp)
target_link_libraries(pz_cell_oracle PRIVATE pzformat)
target_compile_options(pz_cell_oracle PRIVATE -Wall -Wextra -Wpedantic)
