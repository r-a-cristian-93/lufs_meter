cmake_minimum_required(VERSION 3.16)
project(LufsSDL)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 CONFIG REQUIRED)
find_package(SndFile CONFIG REQUIRED)

add_executable(app main.cpp)

target_link_libraries(app
    SDL2::SDL2
    SDL2::SDL2main
    sndfile
    ebur128
)



set(MSYS_BIN "C:/msys64/mingw64/bin")

set(RUNTIME_DLLS
    SDL2.dll
    libsndfile-1.dll
    libebur128.dll

    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libwinpthread-1.dll

    libmpg123-0.dll
    libmp3lame-0.dll
    libogg-0.dll
    libvorbis-0.dll
    libvorbisenc-2.dll
    libFLAC.dll
    libopus-0.dll
)

foreach(DLL ${RUNTIME_DLLS})
    add_custom_command(TARGET app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MSYS_BIN}/${DLL}
        $<TARGET_FILE_DIR:app>
    )
endforeach()
