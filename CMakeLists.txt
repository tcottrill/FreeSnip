cmake_minimum_required(VERSION 3.24)
project(Snip VERSION 1.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# wxWidgets, static, set up like C:\Source2026\Downloader-lite: wxMSW on Windows
# (static CRT, no DLLs), wxGTK 3 on Linux.
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
# PNG (and the zlib it needs) for the clipboard, Save As and portal screenshots.
# Windows uses wx's own copies; Linux the system's, which GTK needs anyway and
# the Flatpak runtime provides (wx's bundled zlib does not build with GCC 14).
if(WIN32)
  set(wx_image_libs builtin)
else()
  set(wx_image_libs sys)
endif()
foreach(feature LIBPNG ZLIB)
  set(wxUSE_${feature} ${wx_image_libs} CACHE STRING "" FORCE)
endforeach()
# DETECT_SM, UIACTIONSIMULATOR and XTEST would link libSM, libICE and libXtst, which a
# Wayland-only system may lack. APPINDICATOR: the Linux tray is Snip's own D-Bus
# StatusNotifierItem (platform_linux.cpp), so wxTaskBarIcon is not used there.
foreach(feature WEBVIEW WEBVIEW_EDGE WEBVIEW_IE WEBREQUEST MEDIACTRL HTML XML XRC RICHTEXT STC AUI RIBBON PROPGRID GL SVG ARCHIVE_STREAMS ZIPSTREAM TARSTREAM JOYSTICK SOCKETS PROTOCOL URL PROTOCOL_FTP PROTOCOL_HTTP PROTOCOL_FILE FS_INET FS_ARCHIVE FS_ZIP SECRETSTORE LIBLZMA GIF PCX TGA IFF PNM ARTPROVIDER_TANGO IPC HELP WXHTML_HELP MS_HTML_HELP DETECT_SM UIACTIONSIMULATOR XTEST APPINDICATOR)
  set(wxUSE_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
if(NOT WIN32)
  set(wxBUILD_TOOLKIT gtk3 CACHE STRING "" FORCE)
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/wxWidgets/CMakeLists.txt")
  message(FATAL_ERROR "wxWidgets is missing from vendor/wxWidgets. It is a git submodule: run\n"
    "  git submodule update --init --recursive\nor clone with --recursive.")
endif()
add_subdirectory(vendor/wxWidgets EXCLUDE_FROM_ALL)

set(sources src/wx/app.cpp src/wx/main_window.cpp src/wx/frozen_desktop.cpp src/wx/image.cpp)
if(WIN32)
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
  add_executable(snip WIN32 ${sources} src/wx/platform_windows.cpp src/monitor_select.cpp "${CMAKE_CURRENT_BINARY_DIR}/snip.rc")
  target_link_libraries(snip PRIVATE wx::core wx::base dwmapi)
  target_compile_options(snip PRIVATE /W4 /utf-8)
  target_link_options(snip PRIVATE /MANIFEST:EMBED "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT snip)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  add_executable(snip ${sources} src/wx/platform_linux.cpp)
  target_link_libraries(snip PRIVATE wx::core wx::base PkgConfig::GTK3)
  target_compile_options(snip PRIVATE -Wall -Wextra)
  install(TARGETS snip RUNTIME DESTINATION bin)
endif()
# Packaging builds with a read-only source tree set their own folder.
set(SNIP_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Folder for the built executable")
set_target_properties(snip PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${SNIP_OUTPUT_DIR}$<$<CONFIG:Debug>:/debug>")
include(CTest)
if(BUILD_TESTING)
  # Shows a synthetic snip in the real window and exits; no capture, no clipboard.
  # Needs a display (on Linux, run the tests from a desktop or WSLg session).
  add_test(NAME smoke_test COMMAND snip --smoke-test)
endif()
