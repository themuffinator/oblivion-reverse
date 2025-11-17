cmake_minimum_required(VERSION 3.16)
project(oblivion_game C)

# Ship a release-configured build by default so the produced DLL matches
# the original Quake II release binaries unless the user explicitly asks
# for another configuration.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Collect every C source under src/game and src/common.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.c"
)

if(GAME_SOURCES STREQUAL "")
    message(FATAL_ERROR "No source files found under src/game")
endif()

add_library(oblivion_game MODULE ${GAME_SOURCES})

target_include_directories(oblivion_game
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/game
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_compile_definitions(oblivion_game
    PRIVATE
        _GNU_SOURCE
)

if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR
            "Quake II expects a 32-bit gamex86.dll. Configure CMake for a Win32 build (e.g. -A Win32).")
    endif()

    # game.def lists the exports that the engine expects on Windows builds.
    target_sources(oblivion_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/game/game.def)
    set_target_properties(oblivion_game PROPERTIES
        PREFIX ""
        OUTPUT_NAME "gamex86"
        RUNTIME_OUTPUT_NAME "gamex86"
        ARCHIVE_OUTPUT_NAME "gamex86"
        SUFFIX ".dll"
    )
else()
    set_target_properties(oblivion_game PROPERTIES
        PREFIX ""
        OUTPUT_NAME "game"
    )
endif()

install(TARGETS oblivion_game
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
