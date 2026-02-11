cmake_minimum_required(VERSION 3.31)

# ВАЖНО: gate для import std должен быть ДО project()
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444" CACHE STRING "" FORCE)
set(CMAKE_CXX_MODULE_STD ON CACHE BOOL "" FORCE)

project(ffmpeg_demo LANGUAGES C CXX)

# чтобы find_package(FFmpeg CONFIG) нашёл cmake/FFmpegConfig.cmake
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
find_package(FFmpeg CONFIG REQUIRED)

add_subdirectory(ffmpeg_cpp)
add_subdirectory(app)
add_subdirectory(mm_pipeline)

include(CTest)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
