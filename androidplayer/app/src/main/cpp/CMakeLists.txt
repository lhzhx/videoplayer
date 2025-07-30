cmake_minimum_required(VERSION 3.22.1)
project("androidplayer")

set(ffmpeg_lib_dir ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libffmpeg-LH.so)
set(ffmpeg_head_dir ${CMAKE_SOURCE_DIR})

include_directories(${ffmpeg_head_dir}/include)
link_directories(${ffmpeg_lib_dir})
find_library(egl-lib EGL)              # EGL 库（用于窗口和上下文管理）
find_library(gles3-lib GLESv3)
find_library(nativewindow-lib nativewindow)
find_library(
        log-lib
        log
)

add_library(
        androidplayer
        SHARED
        src/AAudioRender.cpp
        src/ANWRender.cpp
        src/play.cpp
        src/Demuxer.cpp
        src/Videodecoder.cpp
        src/Videorender.cpp
        src/AudioDecoder.cpp
        src/AudioRingBuffer.cpp
)

target_link_libraries(androidplayer
        android

        log
        aaudio
        ${log-lib}
        ${ffmpeg_lib_dir}
        ${egl-lib}
        ${gles3-lib}
        ${nativewindow-lib}
)