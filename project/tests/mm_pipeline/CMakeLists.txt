include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddCatchTests.cmake)

add_catch_tests(mm_pipeline_tests My::mm_pipeline)

target_sources(mm_pipeline_tests PRIVATE
  async_queue_tests.cpp
)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FfmpegTestRuntime.cmake)
enable_ffmpeg_runtime(mm_pipeline_tests)

if(MSVC)
  target_compile_options(mm_pipeline_tests PRIVATE /W4 /WX)
else()
  target_compile_options(mm_pipeline_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
endif()
