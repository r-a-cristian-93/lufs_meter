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