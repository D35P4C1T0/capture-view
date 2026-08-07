cmake_minimum_required(VERSION 3.25)

project(capture_view
  VERSION 0.1.0
  DESCRIPTION "Low-latency HDMI USB capture-card viewer"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CAPTURE_VIEW_WARNINGS /W4 /permissive-)
else()
  set(CAPTURE_VIEW_WARNINGS -Wall -Wextra -Wpedantic -Wconversion)
endif()

find_package(OpenGL REQUIRED)

if(WIN32)
  find_package(SDL3 CONFIG REQUIRED)
  if(TARGET SDL3::SDL3-static)
    set(CAPTURE_VIEW_SDL_TARGET SDL3::SDL3-static)
  elseif(TARGET SDL3::SDL3-shared)
    set(CAPTURE_VIEW_SDL_TARGET SDL3::SDL3-shared)
  elseif(TARGET SDL3::SDL3)
    set(CAPTURE_VIEW_SDL_TARGET SDL3::SDL3)
  else()
    message(FATAL_ERROR "SDL3 CMake package did not define an SDL3 target")
  endif()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
  pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
  pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
  pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
  pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
  pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
  pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
endif()

set(CAPTURE_VIEW_COMMON_SOURCES
  src/app.cpp
  src/cli.cpp
  src/config.cpp
  src/frame.cpp
  src/log.cpp
  src/main.cpp
  src/test_pattern.cpp
)

if(WIN32)
  list(REMOVE_ITEM CAPTURE_VIEW_COMMON_SOURCES src/app.cpp)
  list(APPEND CAPTURE_VIEW_COMMON_SOURCES
    src/app_windows.cpp
    src/renderer_sdl_windows.cpp
  )
else()
  list(APPEND CAPTURE_VIEW_COMMON_SOURCES
    src/audio_monitor.cpp
    src/audio_devices.cpp
    src/audio_selector.cpp
    src/gtk_ui.cpp
    src/h264_decoder.cpp
    src/mjpeg_decoder.cpp
    src/renderer_sdl.cpp
    src/wizard.cpp
  )
endif()

set(CAPTURE_VIEW_LINUX_SOURCES
  src/v4l2_capture.cpp
  src/v4l2_discovery.cpp
  src/v4l2_output.cpp
)

add_executable(capture-view
  ${CAPTURE_VIEW_COMMON_SOURCES}
  $<$<NOT:$<PLATFORM_ID:Windows>>:${CAPTURE_VIEW_LINUX_SOURCES}>
)
if(WIN32)
  set_target_properties(capture-view PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(capture-view PRIVATE src)
target_link_libraries(capture-view PRIVATE OpenGL::GL)

if(WIN32)
  target_link_libraries(capture-view PRIVATE ${CAPTURE_VIEW_SDL_TARGET})
else()
  target_link_libraries(capture-view PRIVATE
    PkgConfig::SDL3
    PkgConfig::TURBOJPEG
    PkgConfig::PIPEWIRE
    PkgConfig::GTK4
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
  )
endif()

target_compile_options(capture-view PRIVATE ${CAPTURE_VIEW_WARNINGS})

enable_testing()

add_executable(capture-view-tests
  tests/unit_tests.cpp
  src/cli.cpp
  src/frame.cpp
)
target_include_directories(capture-view-tests PRIVATE src)
target_compile_options(capture-view-tests PRIVATE ${CAPTURE_VIEW_WARNINGS})
add_test(NAME capture-view-tests COMMAND capture-view-tests)

install(TARGETS capture-view RUNTIME DESTINATION bin)
install(FILES data/kirsch.bdf DESTINATION share/capture-view)
install(FILES LICENSE DESTINATION share/licenses/capture-view)
install(FILES data/kirsch.OFL.txt DESTINATION share/licenses/capture-view)

if(NOT WIN32)
  install(FILES data/capture-view.desktop DESTINATION share/applications)
  install(FILES data/capture-view.svg DESTINATION share/icons/hicolor/scalable/apps)
  install(FILES docs/capture-view.1 DESTINATION share/man/man1)
endif()
