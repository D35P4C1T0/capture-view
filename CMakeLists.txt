cmake_minimum_required(VERSION 3.25)

project(capture_view
  VERSION 0.1.0
  DESCRIPTION "Low-latency HDMI USB capture-card viewer"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_executable(capture-view
  src/audio_monitor.cpp
  src/audio_devices.cpp
  src/audio_selector.cpp
  src/app.cpp
  src/cli.cpp
  src/config.cpp
  src/frame.cpp
  src/gtk_ui.cpp
  src/log.cpp
  src/main.cpp
  src/mjpeg_decoder.cpp
  src/renderer_sdl.cpp
  src/test_pattern.cpp
  src/v4l2_capture.cpp
  src/v4l2_discovery.cpp
  src/v4l2_output.cpp
  src/wizard.cpp
)

target_include_directories(capture-view PRIVATE src)
target_link_libraries(capture-view PRIVATE
  OpenGL::GL
  PkgConfig::SDL3
  PkgConfig::TURBOJPEG
  PkgConfig::PIPEWIRE
  PkgConfig::GTK4
)

target_compile_options(capture-view PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
)

install(TARGETS capture-view RUNTIME DESTINATION bin)
install(FILES data/capture-view.desktop DESTINATION share/applications)
install(FILES data/capture-view.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES data/kirsch.bdf DESTINATION share/capture-view)
install(FILES docs/capture-view.1 DESTINATION share/man/man1)
install(FILES LICENSE DESTINATION share/licenses/capture-view)
install(FILES data/kirsch.OFL.txt DESTINATION share/licenses/capture-view)
