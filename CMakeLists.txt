cmake_minimum_required(VERSION 3.25)
project(pzmapmaker LANGUAGES CXX)

# Library layer: C++20, no dependencies beyond the standard library.
# (Charter §3, as amended for the C++ port.) The application layer below MAY
# take Qt6 and OpenGL; the library MUST NOT, and nothing above links pzformat
# to Qt.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Library layer: pzformat  (Qt-free, dependency-free)
# ---------------------------------------------------------------------------
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
    tileindex.cpp
    square.cpp
    mapvalidator.cpp
    celleditor.cpp
    mapproject.cpp
)
target_include_directories(pzformat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_options(pzformat PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast
)

# ---------------------------------------------------------------------------
# Tests + oracles  (library-layer, no Qt)
# ---------------------------------------------------------------------------
enable_testing()
function(pz_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pzformat)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

pz_add_test(pz_tests_le_lew       test_le_lew.cpp)
pz_add_test(pz_tests_lotheader    test_lotheader.cpp)
pz_add_test(pz_tests_lotpack      test_lotpack.cpp)
pz_add_test(pz_tests_tiles        test_tiledefs_tilebin.cpp)
pz_add_test(pz_tests_packfile     test_packfile.cpp)
pz_add_test(pz_tests_celldata     test_celldata.cpp)
pz_add_test(pz_tests_tileindex    test_tileindex_square.cpp)
pz_add_test(pz_tests_mapvalidator test_mapvalidator.cpp)
pz_add_test(pz_tests_celleditor   test_celleditor.cpp)
pz_add_test(pz_tests_mapproject   test_mapproject.cpp)

foreach(tool roundtrip oracle tile_oracle pack_oracle cell_oracle classify_oracle edit_oracle)
    add_executable(pz_${tool} ${tool}.cpp)
    target_link_libraries(pz_${tool} PRIVATE pzformat)
    target_compile_options(pz_${tool} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

add_executable(pz_validate validate_cli.cpp)
target_link_libraries(pz_validate PRIVATE pzformat)
target_compile_options(pz_validate PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Application layer: Qt6 shell  (optional — only if Qt6 is found)
# ---------------------------------------------------------------------------
find_package(Qt6 COMPONENTS Widgets OpenGL OpenGLWidgets QUIET)
if(Qt6_FOUND)
    message(STATUS "Qt6 found (${Qt6_VERSION}) — building pzmapmaker app")
    qt_standard_project_setup()
    qt_add_executable(pzmapmaker
        app/main.cpp
        app/mainwindow.cpp
        app/mapview.cpp
    )
    # MainWindow and MapView use Q_OBJECT (signals/slots) -> the meta-object
    # compiler must run. qt_add_executable turns AUTOMOC on, but be explicit so
    # a future reader doesn't wonder why moc output appears.
    set_target_properties(pzmapmaker PROPERTIES AUTOMOC ON)
    target_include_directories(pzmapmaker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    # Qt6::OpenGL pulls in QOpenGLWindow and the GL function wrappers. GL itself
    # comes transitively but link it explicitly for clarity.
    target_link_libraries(pzmapmaker PRIVATE
        pzformat Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
    target_compile_options(pzmapmaker PRIVATE -Wall -Wextra)
else()
    message(STATUS "Qt6 NOT found — skipping pzmapmaker app (library + tests still build)")
endif()
