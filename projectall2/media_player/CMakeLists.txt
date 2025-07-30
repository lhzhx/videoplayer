cmake_minimum_required(VERSION 3.10)
project(media_player)

set(CMAKE_CXX_STANDARD 17)

# 查找依赖库
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENGL REQUIRED gl)
pkg_check_modules(GLEW REQUIRED glew)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(ALSA REQUIRED alsa)
pkg_check_modules(JPEG REQUIRED libjpeg)
pkg_check_modules(PNG REQUIRED libpng)

# 设置包含路径
include_directories(
    ${OPENGL_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    include
)

# 添加可执行文件
add_executable(media_player
    src/main.cpp
    src/FrameQueue.cpp
    src/AudioFrameQueue.cpp
    src/AudioOutput.cpp
    src/network_client.cpp
)

# 链接库 - 只使用 pkg_check_modules 提供的列表
target_link_libraries(media_player
    avformat
    avcodec
    swresample
    swscale
    avutil
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${GLFW_LIBRARIES}
    ${ALSA_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${PNG_LIBRARIES}
    pthread
    dl
)

