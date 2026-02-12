function(enable_ffmpeg_runtime test_target)
  # Путь к bin, где лежат DLL (из импортированной цели)
  get_target_property(_avutil_dll FFmpeg::avutil IMPORTED_LOCATION)
  get_filename_component(_ffmpeg_bin "${_avutil_dll}" DIRECTORY)

  # 1) Для catch_discover_tests уже собранных тестов: добавляем DL_PATHS через свойство
  # Но проще: ты передаёшь DL_PATHS прямо в catch_discover_tests там, где вызываешь.
  # Здесь сделаем только копирование DLL:

  foreach(dep IN ITEMS FFmpeg::avutil FFmpeg::avcodec FFmpeg::avformat FFmpeg::swresample)
    add_custom_command(TARGET ${test_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:${dep}>"
              "$<TARGET_FILE_DIR:${test_target}>"
      VERBATIM
    )
  endforeach()
endfunction()
