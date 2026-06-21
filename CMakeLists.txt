cmake_minimum_required(VERSION 3.16)
project(OmniVerse VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Place the executable in the project root's build/bin for easy packaging.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/Font.cpp
    src/Render.cpp
    src/Sim.cpp
    src/UI.cpp
    src/Audio.cpp
    src/Textures.cpp
    src/Game.cpp
)

# ---------------------------------------------------------------------------
# Android: build the game as libmain.so and compile SDL2 from source as a
# subproject. The Gradle externalNativeBuild invokes this file directly and
# passes SDL_SRC_DIR (see android/fetch-sdl.sh and android/app/build.gradle).
# ---------------------------------------------------------------------------
if(ANDROID)
    if(NOT DEFINED SDL_SRC_DIR OR SDL_SRC_DIR STREQUAL "")
        set(SDL_SRC_DIR "$ENV{SDL_SRC_DIR}")
    endif()
    if(NOT SDL_SRC_DIR OR NOT EXISTS "${SDL_SRC_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "SDL_SRC_DIR must point to the SDL2 source tree. "
            "Run android/fetch-sdl.sh first. Got: '${SDL_SRC_DIR}'")
    endif()

    add_subdirectory("${SDL_SRC_DIR}" "${CMAKE_BINARY_DIR}/SDL")

    # SDLActivity loads libmain.so by default; the target must be named "main".
    add_library(main SHARED ${SOURCES})
    target_compile_options(main PRIVATE -Wall -Wextra -O2)

    if(TARGET SDL2::SDL2)
        target_link_libraries(main PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(main PRIVATE SDL2)
    endif()
    if(TARGET SDL2::SDL2main)
        target_link_libraries(main PRIVATE SDL2::SDL2main)
    endif()

    return()  # skip the desktop executable/discovery below
endif()

add_executable(OmniVerse ${SOURCES})

# ---------------------------------------------------------------------------
# SDL2 discovery: try the CMake config package first (works on Windows / vcpkg
# / MSYS2), then fall back to pkg-config or the FindSDL2 module on Linux/macOS.
# ---------------------------------------------------------------------------
find_package(SDL2 QUIET CONFIG)
if(SDL2_FOUND AND TARGET SDL2::SDL2)
    message(STATUS "Using SDL2 CONFIG package")
    target_link_libraries(OmniVerse PRIVATE SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(OmniVerse PRIVATE SDL2::SDL2main)
    endif()
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
    if(SDL2_FOUND)
        message(STATUS "Using SDL2 via pkg-config")
        target_include_directories(OmniVerse PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(OmniVerse PRIVATE ${SDL2_LIBRARIES})
        target_compile_options(OmniVerse PRIVATE ${SDL2_CFLAGS_OTHER})
    else()
        find_package(SDL2 REQUIRED)
        target_include_directories(OmniVerse PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(OmniVerse PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

if(MSVC)
    target_compile_options(OmniVerse PRIVATE /W3 /O2)
else()
    target_compile_options(OmniVerse PRIVATE -Wall -Wextra -O2)
endif()

# On Windows, copy SDL2.dll next to the executable when we know where it is.
if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET OmniVerse POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:OmniVerse>)
endif()

# Copy the sprite assets next to the executable so they load at runtime
# (the loader looks in <exe-dir>/assets first, then ./assets).
add_custom_command(TARGET OmniVerse POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:OmniVerse>/assets)

install(TARGETS OmniVerse RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
