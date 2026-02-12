add_library(mm_pipeline STATIC)
add_library(My::mm_pipeline ALIAS mm_pipeline)

target_compile_features(mm_pipeline PUBLIC cxx_std_23)

set_target_properties(mm_pipeline PROPERTIES
  # CXX_MODULE_STD ON   # <-- включай только если модули используют import std;
  CXX_SCAN_FOR_MODULES ON
)

target_compile_definitions(mm_pipeline PRIVATE
  __STDC_CONSTANT_MACROS
  __STDC_FORMAT_MACROS
  __STDC_LIMIT_MACROS
)

target_sources(mm_pipeline 
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
    "modules/demuxer.ixx"
    "modules/error.ixx"
    "modules/muxer.ixx"
    "modules/decoder.ixx"
    "modules/encoder.ixx"
  PRIVATE
    "src/demuxer.cpp"
    "src/muxer.cpp"
    "src/decoder.cpp"
    "src/encoder.cpp"
 )

target_include_directories(mm_pipeline
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Если в public headers media_pipeline используются типы ffmpeg_cpp,
# то зависимость должна быть PUBLIC.
target_link_libraries(mm_pipeline PUBLIC
  My::ffmpeg_cpp
)

if(MSVC)
  target_compile_options(mm_pipeline PRIVATE /W4 /WX)
else()
  target_compile_options(mm_pipeline PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
endif()