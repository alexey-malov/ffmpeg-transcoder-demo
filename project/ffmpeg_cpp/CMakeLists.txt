add_library(ffmpeg_cpp STATIC)
add_library(My::ffmpeg_cpp ALIAS ffmpeg_cpp)

target_compile_features(ffmpeg_cpp PUBLIC cxx_std_23)

set_target_properties(ffmpeg_cpp PROPERTIES
  # CXX_MODULE_STD ON   # <-- включай только если модули используют import std;
  CXX_SCAN_FOR_MODULES ON
)

target_compile_definitions(ffmpeg_cpp PRIVATE
  __STDC_CONSTANT_MACROS
  __STDC_FORMAT_MACROS
  __STDC_LIMIT_MACROS
)

target_sources(ffmpeg_cpp
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      "modules/codeccontext.ixx"
      "modules/codec_parameters.ixx"
      "modules/dictionary.ixx"
      "modules/error.ixx"
      "modules/frame.ixx"
      "modules/input_format_context.ixx"
      "modules/output_format_context.ixx"
      "modules/packet.ixx"
      "modules/rational.ixx"
      "modules/rational.io.ixx"
      "modules/version.ixx"
      
  PRIVATE
    "src/avcodec.hpp"
    "src/avformat.hpp"
    "src/avutil.hpp"
    "src/codeccontext.cpp"
    "src/codec_par.hpp"
    "src/dict.hpp"
    "src/dictionary.cpp"
    "src/dictionary.hpp"
    "src/error.cpp"
    "src/frame.cpp"
    "src/frame.hpp"
    "src/input_format_context.cpp"
    "src/output_format_context.cpp"
    "src/packet.cpp" 
    "src/packet.hpp" 
    "src/rational.cpp" 
    "src/rational.hpp"
    "src/version.cpp"
     )

if(MSVC)
  target_compile_options(ffmpeg_cpp PRIVATE /W4 /WX)
else()
  target_compile_options(ffmpeg_cpp PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
endif()

target_link_libraries(ffmpeg_cpp PUBLIC
  FFmpeg::avutil
  FFmpeg::avcodec
  FFmpeg::avformat
  FFmpeg::swresample
)
