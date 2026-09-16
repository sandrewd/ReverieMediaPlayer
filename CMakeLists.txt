cmake_minimum_required(VERSION 3.22)
project(player VERSION 0.3.0 LANGUAGES CXX)

# Shown in About. The homepage is a placeholder until the project actually has a remote -
# better an obvious placeholder than a plausible-looking URL that goes nowhere.
set(PLAYER_HOMEPAGE "https://example.invalid/reverie" CACHE STRING
    "Project homepage shown in the About dialog" FORCE)
set(PLAYER_LICENSE "MIT" CACHE STRING
    "Licence Reverie itself is released under, shown in the About dialog")

string(TIMESTAMP PLAYER_BUILD_DATE "%Y-%m-%d" UTC)
find_package(Git QUIET)
set(PLAYER_GIT_COMMIT "unknown")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE PLAYER_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick OpenGL DBus Svg)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0 gstreamer-app-1.0 gstreamer-audio-1.0 gstreamer-tag-1.0)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
qt_standard_project_setup(REQUIRES 6.4)

# projectM is vendored and built into build/projectm-install (see README).
find_package(projectM4 REQUIRED CONFIG
    PATHS "${CMAKE_SOURCE_DIR}/build/projectm-master-install/lib/cmake/projectM4"
          "${CMAKE_SOURCE_DIR}/build/projectm-install/lib/cmake/projectM4"
    NO_DEFAULT_PATH)

# The playlist library owns preset ordering, shuffle and the automatic advance.
find_package(projectM4Playlist REQUIRED CONFIG
    PATHS "${CMAKE_SOURCE_DIR}/build/projectm-master-install/lib/cmake/projectM4Playlist"
          "${CMAKE_SOURCE_DIR}/build/projectm-install/lib/cmake/projectM4Playlist"
    NO_DEFAULT_PATH)

# projectM only gained an explicit render target in 4.2.0; without it the visualizer
# renders to the default framebuffer and the QML item stays black.
include(CheckCXXSourceCompiles)
get_target_property(_pm_includes libprojectM::projectM INTERFACE_INCLUDE_DIRECTORIES)
find_file(PROJECTM_RENDER_OPENGL_H "projectM-4/render_opengl.h" PATHS ${_pm_includes} NO_DEFAULT_PATH)
if(PROJECTM_RENDER_OPENGL_H)
    file(READ "${PROJECTM_RENDER_OPENGL_H}" _pm_render_hdr)
    string(FIND "${_pm_render_hdr}" "projectm_opengl_render_frame_fbo" _pm_has_fbo)
    if(NOT _pm_has_fbo EQUAL -1)
        set(PROJECTM_HAS_RENDER_FRAME_FBO ON)
    endif()
endif()
if(PROJECTM_HAS_RENDER_FRAME_FBO)
    message(STATUS "projectM: using projectm_opengl_render_frame_fbo")
else()
    message(WARNING "projectM lacks projectm_opengl_render_frame_fbo; the visualizer will render black")
endif()

qt_add_executable(player
    src/main.cpp
    src/ProjectMItem.cpp
    src/ProjectMItem.h
    src/AudioEngine.cpp
    src/AudioEngine.h
    src/AudioRingBuffer.h
    src/PlaylistModel.cpp
    src/PlaylistModel.h
    src/MprisAdaptor.cpp
    src/MprisAdaptor.h
    src/SystemTheme.cpp
    src/SystemTheme.h
    src/PresetLibrary.cpp
    src/PresetLibrary.h
    src/AppInfo.cpp
    src/AppInfo.h
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Launcher icon and desktop entry, freedesktop hicolor layout. The PNGs are also compiled
# into the binary below so an uninstalled build still has a window icon.
include(GNUInstallDirs)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/share/icons
        DESTINATION ${CMAKE_INSTALL_DATADIR})
install(FILES ${CMAKE_SOURCE_DIR}/assets/share/applications/reverie.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(TARGETS player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(CODE "execute_process(COMMAND gtk-update-icon-cache -q -t -f
        \"$ENV{DESTDIR}${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor\" ERROR_QUIET)")

qt_add_resources(player "branding"
    PREFIX "/branding"
    BASE "${CMAKE_SOURCE_DIR}/assets/share/icons/hicolor"
    FILES
        assets/share/icons/hicolor/16x16/apps/reverie.png
        assets/share/icons/hicolor/22x22/apps/reverie.png
        assets/share/icons/hicolor/24x24/apps/reverie.png
        assets/share/icons/hicolor/32x32/apps/reverie.png
        assets/share/icons/hicolor/48x48/apps/reverie.png
        assets/share/icons/hicolor/64x64/apps/reverie.png
        assets/share/icons/hicolor/128x128/apps/reverie.png
        assets/share/icons/hicolor/256x256/apps/reverie.png
        assets/share/icons/hicolor/scalable/apps/reverie.svg
)

qt_add_qml_module(player
    URI Player
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/IconButton.qml
        qml/TransportBar.qml
        qml/PlaylistPanel.qml
        qml/ColourRow.qml
        qml/ReverieMark.qml
)

set_target_properties(player PROPERTIES OUTPUT_NAME reverie)

target_include_directories(player PRIVATE src)
target_compile_definitions(player PRIVATE
    PLAYER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    PLAYER_VERSION="${PROJECT_VERSION}"
    PLAYER_BUILD_DATE="${PLAYER_BUILD_DATE}"
    PLAYER_GIT_COMMIT="${PLAYER_GIT_COMMIT}"
    PLAYER_HOMEPAGE="${PLAYER_HOMEPAGE}"
    PLAYER_LICENSE="${PLAYER_LICENSE}")

if(PROJECTM_HAS_RENDER_FRAME_FBO)
    target_compile_definitions(player PRIVATE PROJECTM_HAS_RENDER_FRAME_FBO)
endif()

target_link_libraries(player PRIVATE
    Qt6::Quick
    Qt6::OpenGL
    Qt6::DBus
    Qt6::Svg
    PkgConfig::GST
    PkgConfig::TAGLIB
    libprojectM::projectM
    libprojectM::playlist
)
