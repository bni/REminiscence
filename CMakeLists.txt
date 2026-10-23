cmake_minimum_required(VERSION 3.19)
project(REminiscence)

find_package(SDL3 REQUIRED)

add_definitions(-DUSE_MODPLUG -DUSE_STB_VORBIS -DUSE_ZLIB -DUSE_MIDI_DRIVER -DUSE_MT32EMU)

find_path(
        MODPLUG_INCLUDE_DIR
        NAMES libmodplug/modplug.h
)

find_library(
        MODPLUG_LIBRARY
        NAMES libmodplug.dylib
)

find_path(
        ZLIB_INCLUDE_DIR
        NAMES zlib.h
)

find_library(
        ZLIB_LIBRARY
        NAMES libz.dylib
)

find_path(
        MT32EMU_INCLUDE_DIR
        NAMES mt32emu.h
)

find_library(
        MT32EMU_LIBRARY
        NAMES libmt32emu.dylib
)

include_directories(
        ${SDL3_INCLUDE_DIRS}
        ${MODPLUG_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}
        ${MT32EMU_INCLUDE_DIR}
)

add_executable(
        rs
        collision.cpp
        cpc_player.cpp
        cutscene.cpp
        decode_mac.cpp
        file.cpp
        fs.cpp
        game.cpp
        graphics.cpp
        main.cpp
        menu.cpp
        midi_driver_adlib.cpp
        midi_driver_mt32.cpp
        midi_parser.cpp
        mixer.cpp
        mod_player.cpp
        ogg_player.cpp
        piege.cpp
        prf_player.cpp
        protection.cpp
        resource.cpp
        resource_aba.cpp
        resource_mac.cpp
        resource_paq.cpp
        scaler.cpp
        screenshot.cpp
        shader.cpp
        seq_player.cpp
        sfx_player.cpp
        staticres.cpp
        systemstub_sdl.cpp
        unpack.cpp
        util.cpp
        video.cpp
)

target_link_libraries(
        rs
        ${SDL3_LIBRARIES}
        ${MODPLUG_LIBRARY}
        ${ZLIB_LIBRARY}
        ${MT32EMU_LIBRARY}
)

configure_file(rs.cfg rs.cfg COPYONLY)
configure_file(shader.metal shader.metal COPYONLY)
configure_file(MT32_CONTROL.ROM MT32_CONTROL.ROM COPYONLY)
configure_file(MT32_PCM.ROM MT32_PCM.ROM COPYONLY)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/DATA/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/DATA/)
