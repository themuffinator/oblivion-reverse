cmake_minimum_required(VERSION 3.16)
project(oblivion_game C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Collect every C source under src/game.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/*.c"
)

if(GAME_SOURCES STREQUAL "")
    message(FATAL_ERROR "No source files found under src/game")
endif()

add_library(oblivion_game MODULE ${GAME_SOURCES})

target_include_directories(oblivion_game
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/game
)

target_compile_definitions(oblivion_game
    PRIVATE
        _GNU_SOURCE
)

if(WIN32)
    # game.def lists the exports that the engine expects on Windows builds.
    target_sources(oblivion_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/game/game.def)
    set_target_properties(oblivion_game PROPERTIES
        PREFIX ""
        OUTPUT_NAME "game"
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
