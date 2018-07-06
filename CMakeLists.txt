cmake_minimum_required(VERSION 3.9)
project(kys)

include_directories(
        /usr/local/include
        common
        src
        bass24-osx
        bassmidi24-osx
)

link_directories(
        /usr/local/lib
        bass24-osx
        bassmidi24-osx
)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -v")

# 将 src/lib 下面的所有 .c .cpp .h 作为变量 LIBRARY_SOURCE_FILES 的值
file(GLOB SOURCE_FILES_MAIN src/*.cpp)
file(GLOB SOURCE_FILES_COMMON common/libconvert.cpp common/File.cpp)
# 默认添加库代码
set(SOURCE_FILES ${SOURCE_FILES_MAIN} ${SOURCE_FILES_COMMON})

add_executable(kys ${SOURCE_FILES})
target_link_libraries(kys SDL2 SDL2_ttf SDL2_image SDL2main lua5.3 bass bassmidi iconv minizip)