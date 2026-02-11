function(add_catch_tests test_target lib_target)
  add_executable(${test_target})
  target_compile_features(${test_target} PRIVATE cxx_std_23)
  target_link_libraries(${test_target} PRIVATE Catch2::Catch2WithMain ${lib_target})

  get_target_property(_avutil_dll FFmpeg::avutil IMPORTED_LOCATION)
  get_filename_component(_ffmpeg_bin "${_avutil_dll}" DIRECTORY)

  catch_discover_tests(${test_target}
    DL_PATHS
      "${_ffmpeg_bin}"
  )
endfunction()
