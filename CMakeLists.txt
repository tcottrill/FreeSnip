cmake_minimum_required(VERSION 3.24)
project(Snip VERSION 1.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)
  # wxWidgets (static, static CRT), set up like C:\Source2026\Downloader-lite.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
  set(wxBUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(wxBUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
  set(wxBUILD_LOCALES OFF CACHE BOOL "" FORCE)
  set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)
  foreach(feature LIBJPEG LIBTIFF LIBWEBP NANOSVG EXPAT REGEX)
    set(wxUSE_${feature} OFF CACHE STRING "" FORCE)
  endforeach()
  # PNG for the clipboard and Save As; wx's own copies of libpng and zlib.
  foreach(feature LIBPNG ZLIB)
    set(wxUSE_${feature} builtin CACHE STRING "" FORCE)
  endforeach()
  foreach(feature WEBVIEW WEBVIEW_EDGE WEBVIEW_IE WEBREQUEST MEDIACTRL HTML XML XRC RICHTEXT STC AUI RIBBON PROPGRID GL SVG ARCHIVE_STREAMS ZIPSTREAM TARSTREAM JOYSTICK SOCKETS PROTOCOL URL PROTOCOL_FTP PROTOCOL_HTTP PROTOCOL_FILE FS_INET FS_ARCHIVE FS_ZIP SECRETSTORE LIBLZMA GIF PCX TGA IFF PNM ARTPROVIDER_TANGO IPC HELP WXHTML_HELP MS_HTML_HELP DETECT_SM UIACTIONSIMULATOR XTEST)
    set(wxUSE_${feature} OFF CACHE BOOL "" FORCE)
  endforeach()
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/wxWidgets/CMakeLists.txt")
    message(FATAL_ERROR "wxWidgets is missing from vendor/wxWidgets. It is a git submodule: run\n"
      "  git submodule update --init --recursive\nor clone with --recursive.")
  endif()
  add_subdirectory(vendor/wxWidgets EXCLUDE_FROM_ALL)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  set(rc_version "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
  file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/snip.rc" CONTENT
"1 VERSIONINFO
FILEVERSION ${rc_version}
PRODUCTVERSION ${rc_version}
BEGIN
  BLOCK \"StringFileInfo\"
  BEGIN
    BLOCK \"040904B0\"
    BEGIN
      VALUE \"FileDescription\", \"Snip\"
      VALUE \"FileVersion\", \"${PROJECT_VERSION}\"
      VALUE \"InternalName\", \"snip\"
      VALUE \"OriginalFilename\", \"snip.exe\"
      VALUE \"ProductName\", \"Snip\"
      VALUE \"ProductVersion\", \"${PROJECT_VERSION}\"
    END
  END
  BLOCK \"VarFileInfo\"
  BEGIN
    VALUE \"Translation\", 0x409, 1200
  END
END
")
  add_executable(snip WIN32 src/wx/app.cpp src/wx/main_window.cpp src/wx/frozen_desktop.cpp src/wx/image.cpp
    src/wx/platform_windows.cpp src/monitor_select.cpp "${CMAKE_CURRENT_BINARY_DIR}/snip.rc")
  target_link_libraries(snip PRIVATE wx::core wx::base dwmapi)
  target_compile_options(snip PRIVATE /W4 /utf-8)
  target_link_options(snip PRIVATE /MANIFEST:EMBED "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
  set(SNIP_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Folder for the built executable")
  set_target_properties(snip PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${SNIP_OUTPUT_DIR}$<$<CONFIG:Debug>:/debug>")
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT snip)
  include(CTest)
  if(BUILD_TESTING)
    # Shows a synthetic snip in the real window and exits; no capture, no clipboard.
    add_test(NAME smoke_test COMMAND snip --smoke-test)
  endif()
  return()
endif()
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets DBus Network Test)
include(CTest)
add_library(snip_image src/qt/image_ops.cpp src/qt/selection.cpp src/qt/frozen_desktop.cpp)
target_include_directories(snip_image PUBLIC src/qt)
target_link_libraries(snip_image PUBLIC Qt6::Widgets)
add_library(snip_portal src/qt/portal.cpp src/qt/shortcuts.cpp)
target_include_directories(snip_portal PUBLIC src/qt)
target_link_libraries(snip_portal PUBLIC Qt6::Widgets Qt6::DBus)
add_executable(snip src/qt/main.cpp src/qt/main_window.cpp)
target_link_libraries(snip PRIVATE snip_image snip_portal Qt6::Network)
target_compile_options(snip PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS snip RUNTIME DESTINATION bin)
if(BUILD_TESTING)
  add_executable(frozen_tests tests/frozen_tests.cpp)
  target_link_libraries(frozen_tests PRIVATE snip_image Qt6::Test)
  add_test(NAME frozen_tests COMMAND frozen_tests)
  set_tests_properties(frozen_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME instance_test COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/instance_test.py $<TARGET_FILE:snip>)
  add_executable(image_tests tests/image_tests.cpp)
  target_link_libraries(image_tests PRIVATE snip_image Qt6::Test)
  add_test(NAME image_tests COMMAND image_tests)
  set_tests_properties(image_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(selection_tests tests/selection_tests.cpp)
  target_link_libraries(selection_tests PRIVATE snip_image Qt6::Test)
  add_test(NAME selection_tests COMMAND selection_tests)
  set_tests_properties(selection_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(portal_tests tests/portal_tests.cpp)
  target_link_libraries(portal_tests PRIVATE snip_portal Qt6::Test)
  add_test(NAME portal_tests COMMAND portal_tests)
  set_tests_properties(portal_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  add_executable(shortcut_tests tests/shortcut_tests.cpp)
  target_link_libraries(shortcut_tests PRIVATE snip_portal Qt6::Test)
  add_test(NAME shortcut_tests COMMAND shortcut_tests)
  set_tests_properties(shortcut_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  add_executable(ui_tests tests/ui_tests.cpp src/qt/main_window.cpp)
  target_link_libraries(ui_tests PRIVATE snip_image snip_portal Qt6::Test)
  add_test(NAME ui_tests COMMAND ui_tests)
  set_tests_properties(ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()
