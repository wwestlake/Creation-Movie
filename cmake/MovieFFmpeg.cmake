set(MOVIE_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/ffmpeg-n8.1.3-18-g46d899d7a0-win64-lgpl-shared-8.1" CACHE PATH "Pinned LGPL shared FFmpeg SDK")
if(NOT EXISTS "${MOVIE_FFMPEG_ROOT}/include/libavformat/avformat.h")
    message(FATAL_ERROR "Run scripts/Fetch-FFmpeg.ps1, or set MOVIE_FFMPEG_ROOT to the pinned shared LGPL SDK. No automatic toolchain build is performed.")
endif()
add_library(movie_ffmpeg INTERFACE)
target_include_directories(movie_ffmpeg INTERFACE "${MOVIE_FFMPEG_ROOT}/include")
foreach(lib avformat avcodec avutil swscale swresample)
    target_link_libraries(movie_ffmpeg INTERFACE "${MOVIE_FFMPEG_ROOT}/lib/${lib}.lib")
endforeach()
file(GLOB MOVIE_FFMPEG_DLLS "${MOVIE_FFMPEG_ROOT}/bin/*.dll")
function(movie_copy_ffmpeg target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MOVIE_FFMPEG_DLLS} "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/FFmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MOVIE_FFMPEG_ROOT}/LICENSE.txt" "$<TARGET_FILE_DIR:${target}>/licenses/FFmpeg/"
        VERBATIM)
endfunction()
