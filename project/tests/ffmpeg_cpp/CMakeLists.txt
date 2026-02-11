include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddCatchTests.cmake)

add_catch_tests(ffmpeg_cpp_tests My::ffmpeg_cpp)

target_sources(ffmpeg_cpp_tests PRIVATE
  rational_tests.cpp
  unique_handle_tests.cpp
  dictionary_tests.cpp)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FfmpegTestRuntime.cmake)
enable_ffmpeg_runtime(ffmpeg_cpp_tests)

if(MSVC)
  target_compile_options(ffmpeg_cpp_tests PRIVATE /W4 /WX)
else()
  target_compile_options(ffmpeg_cpp_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
endif()
