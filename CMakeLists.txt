cmake_minimum_required(VERSION 3.16)
project(dos_gta_recomp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(SDL2_TARGET PkgConfig::SDL2)
else()
    if(TARGET SDL2::SDL2)
        set(SDL2_TARGET SDL2::SDL2)
    else()
        set(SDL2_TARGET ${SDL2_LIBRARIES})
    endif()
endif()

if(NOT ANDROID)
    find_package(OpenGL REQUIRED)
endif()
if(WIN32)
    find_package(GLEW REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(GTA_SOURCES
    src/main.cpp
    src/core/config.cpp
    src/core/game.cpp
    src/graphics/gl.cpp
    src/graphics/shader.cpp
    src/graphics/texture.cpp
    src/graphics/renderer.cpp
    src/graphics/camera.cpp
    src/graphics/font.cpp
    src/graphics/bitmap_font.cpp
    src/input/input_manager.cpp
    src/audio/audio_manager.cpp
    src/data/formats.cpp
    src/data/asset_manager.cpp
    src/game/world.cpp
    src/game/vehicle.cpp
)

add_executable(dos_gta_recomp ${GTA_SOURCES})

target_include_directories(dos_gta_recomp PRIVATE src)

target_link_libraries(dos_gta_recomp PRIVATE ${SDL2_TARGET})
if(WIN32 AND TARGET SDL2::SDL2main)
    target_link_libraries(dos_gta_recomp PRIVATE SDL2::SDL2main)
endif()

if(ANDROID)
    target_compile_definitions(dos_gta_recomp PRIVATE GTA_GLES=1)
    target_link_libraries(dos_gta_recomp PRIVATE GLESv3 EGL log android)
elseif(WIN32)
    target_link_libraries(dos_gta_recomp PRIVATE OpenGL::GL GLEW::GLEW)
else()
    target_link_libraries(dos_gta_recomp PRIVATE OpenGL::GL)
endif()

if(APPLE)
    target_compile_definitions(dos_gta_recomp PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(dos_gta_recomp PRIVATE
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
elseif(ANDROID)
    # GLESv3/EGL/log are linked above.
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(dos_gta_recomp PRIVATE Threads::Threads)
endif()

# Default data path, overridable at configure time:
#   cmake .. -DGTA_DATA_DIR=/path/to/GTADATA
set(GTA_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/game_data/GTADATA" CACHE PATH "Path to the GTA data directory")
target_compile_definitions(dos_gta_recomp PRIVATE GTA_DEFAULT_DATA_DIR="${GTA_DATA_DIR}")

if(MSVC)
    target_compile_options(dos_gta_recomp PRIVATE /W4)
else()
    target_compile_options(dos_gta_recomp PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS dos_gta_recomp RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/dos_gta_recomp/assets)

# ---------------------------------------------------------------------------
# Tests (format parsers are also exercised by the Python tools)
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_executable(format_tests tests/test_formats.cpp
        src/data/formats.cpp
    )
    target_include_directories(format_tests PRIVATE src)
    target_compile_definitions(format_tests PRIVATE GTA_DEFAULT_DATA_DIR="${GTA_DATA_DIR}")
    add_test(NAME formats COMMAND format_tests)
endif()
