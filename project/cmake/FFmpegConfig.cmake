# FFmpegConfig.cmake
# Requires:
#   -DFFMPEG_ROOT=C:/sdk/ffmpeg-msvc

if(NOT DEFINED FFMPEG_ROOT)
  if(DEFINED ENV{FFMPEG_ROOT})
    set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
  else()
    message(FATAL_ERROR
      "FFMPEG_ROOT is not set.\n"
      "Set it via:\n"
      "  -DFFMPEG_ROOT=...\n"
      "or environment variable FFMPEG_ROOT"
    )
  endif()
endif()

if(NOT DEFINED FFMPEG_ROOT)
  message(FATAL_ERROR "FFMPEG_ROOT is not set. Example: -DFFMPEG_ROOT=C:/deps/ffmpeg-msvc")
endif()

set(_ff_inc "${FFMPEG_ROOT}/include")
set(_ff_lib "${FFMPEG_ROOT}/lib")
set(_ff_bin "${FFMPEG_ROOT}/bin")

foreach(p IN ITEMS "${_ff_inc}" "${_ff_lib}" "${_ff_bin}")
  if(NOT EXISTS "${p}")
    message(FATAL_ERROR "FFmpeg path not found: ${p}")
  endif()
endforeach()

function(_ff_import tgt lib dll)
  # Import lib (.lib)
  set(_implib "${_ff_lib}/${lib}")
  if(NOT EXISTS "${_implib}")
    message(FATAL_ERROR "FFmpeg import lib not found: ${_implib}")
  endif()

  # Runtime DLL
  set(_dll "${_ff_bin}/${dll}")
  if(NOT EXISTS "${_dll}")
    message(FATAL_ERROR "FFmpeg DLL not found: ${_dll}")
  endif()

  add_library(FFmpeg::${tgt} SHARED IMPORTED GLOBAL)
  set_target_properties(FFmpeg::${tgt} PROPERTIES
    IMPORTED_IMPLIB "${_implib}"
    IMPORTED_LOCATION "${_dll}"
    INTERFACE_INCLUDE_DIRECTORIES "${_ff_inc}"
  )
endfunction()

# Основные библиотеки (почти всем нужны)
_ff_import(avutil     "avutil.lib"     "avutil-59.dll")
_ff_import(avcodec    "avcodec.lib"    "avcodec-61.dll")
_ff_import(avformat   "avformat.lib"   "avformat-61.dll")
_ff_import(swscale    "swscale.lib"    "swscale-8.dll")

# Часто используемые дополнительные
_ff_import(swresample "swresample.lib" "swresample-5.dll")
_ff_import(avfilter   "avfilter.lib"   "avfilter-10.dll")
_ff_import(avdevice   "avdevice.lib"   "avdevice-61.dll")
_ff_import(postproc   "postproc.lib"   "postproc-58.dll")

# Удобная агрегирующая цель (линкуй её, если не хочешь перечислять всё)
add_library(FFmpeg::ffmpeg INTERFACE IMPORTED GLOBAL)
target_link_libraries(FFmpeg::ffmpeg INTERFACE
  FFmpeg::avutil
  FFmpeg::avcodec
  FFmpeg::avformat
  FFmpeg::swscale
  FFmpeg::swresample
  FFmpeg::avfilter
  FFmpeg::avdevice
  FFmpeg::postproc
)
