add_executable(ffmpeg_demo)

target_compile_features(ffmpeg_demo PRIVATE cxx_std_23)

target_sources(ffmpeg_demo PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)

if(MSVC)
  target_compile_options(ffmpeg_demo PRIVATE /W4 /WX)
else()
  target_compile_options(ffmpeg_demo PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
endif()

target_link_libraries(ffmpeg_demo PRIVATE My::mm_pipeline)

# Transcode demo executable
add_executable(transcode_demo)

target_compile_features(transcode_demo PRIVATE cxx_std_23)

target_sources(transcode_demo PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/transcode_demo.cpp
)

if(MSVC)
  target_compile_options(transcode_demo PRIVATE /W4 /WX)
else()
  target_compile_options(transcode_demo PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
endif()

target_link_libraries(transcode_demo PRIVATE My::mm_pipeline)

function(copy_runtime_dlls target)
  foreach(lib IN LISTS ARGN)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:${lib}>"
              "$<TARGET_FILE_DIR:${target}>"
      VERBATIM
    )
  endforeach()
endfunction()

copy_runtime_dlls(ffmpeg_demo
  FFmpeg::avutil
  FFmpeg::avcodec
  FFmpeg::avformat
  FFmpeg::swresample
)

copy_runtime_dlls(transcode_demo
  FFmpeg::avutil
  FFmpeg::avcodec
  FFmpeg::avformat
  FFmpeg::swresample
)


