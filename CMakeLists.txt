cmake_minimum_required(VERSION 3.22)
project(player VERSION 0.8.2 LANGUAGES CXX)

# Shown in About. Real since 2026-09-16; it was deliberately an obvious placeholder until the
# project had a remote, because a plausible-looking URL that goes nowhere is worse than one that
# announces itself. FORCE so an older cache carrying the placeholder is overwritten.
set(PLAYER_HOMEPAGE "https://github.com/sandrewd/ReverieMediaPlayer" CACHE STRING
    "Project homepage shown in the About dialog" FORCE)
# Reverse-DNS application ID, derived from the repository host. Flathub requires the ID to match
# a domain or forge account the project actually controls, and it is also the .desktop, icon and
# metainfo basename once packaged there.
set(PLAYER_APP_ID "io.github.sandrewd.ReverieMediaPlayer" CACHE STRING
    "Reverse-DNS application ID used for packaging")
set(PLAYER_LICENSE "MIT" CACHE STRING
    "Licence Reverie itself is released under, shown in the About dialog")

option(PLAYER_EMBED_SOURCE_DIR
    "Compile the source tree path into the binary so it can find assets without installing" OFF)
if(PLAYER_EMBED_SOURCE_DIR)
    set(PLAYER_EMBEDDED_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
else()
    set(PLAYER_EMBEDDED_SOURCE_DIR "")
endif()

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
    gstreamer-1.0 gstreamer-app-1.0 gstreamer-audio-1.0 gstreamer-tag-1.0 gstreamer-video-1.0)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
qt_standard_project_setup(REQUIRES 6.4)

# projectM is vendored and built into build/projectm-install (see README) - but not everywhere.
# A Flatpak builds it into the same prefix as Reverie, so the vendored tree does not exist and
# an unqualified lookup is the only one that can succeed. Look in the build tree first, because
# that is what a developer wants when both are present, then fall back to the normal search.
find_package(projectM4 QUIET CONFIG
    PATHS "${CMAKE_SOURCE_DIR}/build/projectm-master-install/lib/cmake/projectM4"
          "${CMAKE_SOURCE_DIR}/build/projectm-install/lib/cmake/projectM4"
    NO_DEFAULT_PATH)
if(NOT projectM4_FOUND)
    find_package(projectM4 REQUIRED CONFIG)
endif()

# The playlist library owns preset ordering, shuffle and the automatic advance.
find_package(projectM4Playlist QUIET CONFIG
    PATHS "${CMAKE_SOURCE_DIR}/build/projectm-master-install/lib/cmake/projectM4Playlist"
          "${CMAKE_SOURCE_DIR}/build/projectm-install/lib/cmake/projectM4Playlist"
    NO_DEFAULT_PATH)
if(NOT projectM4Playlist_FOUND)
    find_package(projectM4Playlist REQUIRED CONFIG)
endif()

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
    src/VideoItem.cpp
    src/VideoItem.h
    src/Logging.cpp
    src/Logging.h
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Launcher icon and desktop entry, freedesktop hicolor layout. The PNGs are also compiled
# into the binary below so an uninstalled build still has a window icon.
include(GNUInstallDirs)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/share/icons
        DESTINATION ${CMAKE_INSTALL_DATADIR})
install(FILES ${CMAKE_SOURCE_DIR}/assets/share/applications/${PLAYER_APP_ID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# AppStream metadata. Required by Flathub, and what software centres read to describe the
# application; harmless and useful in the .deb too.
install(FILES ${CMAKE_SOURCE_DIR}/assets/share/metainfo/${PLAYER_APP_ID}.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(TARGETS player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The preset library has to be installed, not merely found in the source tree: PLAYER_SOURCE_DIR
# is a developer convenience and means nothing on anyone else's machine.
#
# What ships is the curated 480, about 5 MB. The full corpus is 157 MB and 9,795 files, which is
# not a reasonable download for a media player and would dominate any package we build. The full
# pack stays an optional extra - see the brief - and when only the curated set is installed the
# curated *list* is deliberately left out, so the menu item that switches between the two hides
# itself instead of appearing to do nothing.
set(PLAYER_PRESET_SOURCE "${CMAKE_SOURCE_DIR}/assets/presets")
set(PLAYER_PRESET_STAGE "${CMAKE_BINARY_DIR}/staged-presets")
set(PLAYER_CURATED_LIST "${CMAKE_SOURCE_DIR}/assets/presets-curated.txt")

if(EXISTS "${PLAYER_PRESET_SOURCE}" AND EXISTS "${PLAYER_CURATED_LIST}")
    file(STRINGS "${PLAYER_CURATED_LIST}" PLAYER_CURATED_ENTRIES)
    file(REMOVE_RECURSE "${PLAYER_PRESET_STAGE}")
    set(_staged 0)
    set(_missing 0)
    foreach(_rel IN LISTS PLAYER_CURATED_ENTRIES)
        if(_rel MATCHES "^[ 	]*#" OR _rel STREQUAL "")
            continue()
        endif()
        if(EXISTS "${PLAYER_PRESET_SOURCE}/${_rel}")
            get_filename_component(_sub "${_rel}" DIRECTORY)
            file(COPY "${PLAYER_PRESET_SOURCE}/${_rel}"
                 DESTINATION "${PLAYER_PRESET_STAGE}/${_sub}")
            math(EXPR _staged "${_staged} + 1")
        else()
            math(EXPR _missing "${_missing} + 1")
        endif()
    endforeach()
    message(STATUS "Reverie: staged ${_staged} curated presets for install (${_missing} missing)")
    install(DIRECTORY "${PLAYER_PRESET_STAGE}/"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/reverie/presets)
else()
    message(WARNING "Reverie: no preset corpus at ${PLAYER_PRESET_SOURCE}; "
                    "the installed application will have no visualisations")
endif()

# projectM is vendored and built alongside us, so it is not on the system loader path. Install
# its libraries into a private directory and point the executable at them relatively - an
# absolute build-tree RPATH would work on this machine and nowhere else, which is exactly the
# kind of thing that only shows up once someone installs it.
get_target_property(_pm_lib libprojectM::projectM IMPORTED_LOCATION_RELEASE)
if(NOT _pm_lib)
    get_target_property(_pm_lib libprojectM::projectM IMPORTED_LOCATION)
endif()
get_filename_component(_pm_libdir "${_pm_lib}" DIRECTORY)
file(GLOB PROJECTM_RUNTIME_LIBS "${_pm_libdir}/libprojectM-4*.so*")
install(FILES ${PROJECTM_RUNTIME_LIBS}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/reverie)
set_target_properties(player PROPERTIES
    INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/reverie"
    BUILD_WITH_INSTALL_RPATH FALSE)
install(CODE "execute_process(COMMAND gtk-update-icon-cache -q -t -f
        \"$ENV{DESTDIR}${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor\" ERROR_QUIET)")

qt_add_resources(player "branding"
    PREFIX "/branding"
    BASE "${CMAKE_SOURCE_DIR}/assets/share/icons/hicolor"
    FILES
        assets/share/icons/hicolor/16x16/apps/io.github.sandrewd.ReverieMediaPlayer.png
        assets/share/icons/hicolor/22x22/apps/io.github.sandrewd.ReverieMediaPlayer.png
        assets/share/icons/hicolor/24x24/apps/io.github.sandrewd.ReverieMediaPlayer.png
        assets/share/icons/hicolor/32x32/apps/io.github.sandrewd.ReverieMediaPlayer.png
        assets/share/icons/hicolor/48x48/apps/io.github.sandrewd.ReverieMediaPlayer.png
        assets/share/icons/hicolor/64x64/apps/io.github.sandrewd.ReverieMediaPlayer.png
        assets/share/icons/hicolor/128x128/apps/io.github.sandrewd.ReverieMediaPlayer.png
        assets/share/icons/hicolor/256x256/apps/io.github.sandrewd.ReverieMediaPlayer.png
        assets/share/icons/hicolor/scalable/apps/io.github.sandrewd.ReverieMediaPlayer.svg
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
        qml/VideoSurface.qml
        qml/Ticker.qml
)

set_target_properties(player PROPERTIES OUTPUT_NAME reverie)

target_include_directories(player PRIVATE src)
target_compile_definitions(player PRIVATE
    # Empty unless explicitly asked for. This is a developer convenience - it lets the binary
    # find assets in the source tree without installing - but it is an absolute path from the
    # build machine, and Qt stores it as UTF-16, so it is invisible to `strings` while still
    # sitting in any distributed binary for anyone who looks. A shipped package should not carry
    # its author's home directory. Configure with -DPLAYER_EMBED_SOURCE_DIR=ON when working from
    # a build tree.
    PLAYER_SOURCE_DIR="${PLAYER_EMBEDDED_SOURCE_DIR}"
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

# --- packaging ------------------------------------------------------------------------------
#
# A .deb first, deliberately, even though Flatpak is the distribution goal. The immediate need is
# a second machine to test A/V sync on, and a sandbox introduces audio routing through portals,
# GL driver access and bus-name permissions - all of which would muddy exactly that measurement.
# Zorin 18 and Mint 22.3 are both Ubuntu 24.04, so one .deb covers both targets.
#
# projectM is vendored into a private ${CMAKE_INSTALL_LIBDIR}/reverie, so dpkg-shlibdeps has to be
# told where it is or it reports our own libraries as unresolved dependencies.
set(CPACK_PACKAGE_NAME "reverie")
set(CPACK_PACKAGE_VENDOR "Reverie")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Media player with built-in Milkdrop-style visualisations")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PLAYER_HOMEPAGE}")
set(CPACK_PACKAGE_CONTACT "Reverie Media Player <sandrewd@users.noreply.github.com>")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# The build tree's copy, not the install prefix: at package time nothing has been installed yet,
# and pointing dpkg-shlibdeps at a directory that does not exist fails the whole run.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${_pm_libdir}")

# Runtime dependencies shlibdeps cannot see, because nothing in the ELF headers mentions them.
#
# QML modules are resolved by name at runtime and never linked. Listing them by hand shipped a
# package missing QtQuick.Templates and QtQuick.Shapes: qml6-module-qtquick-controls depends on
# the templates *library* but not its QML module, and a developer machine has both anyway through
# the Qt dev packages, so the gap only appears on a clean install - as "plugin not found" at QML
# load, with the application dying before it draws anything. They are derived now: see
# tools/qml-package-deps.py. The static list survives only as a fallback for build hosts without
# qmlimportscanner or dpkg.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env python3
            "${CMAKE_SOURCE_DIR}/tools/qml-package-deps.py" "${CMAKE_SOURCE_DIR}/qml"
    OUTPUT_VARIABLE PLAYER_QML_PACKAGES
    ERROR_VARIABLE PLAYER_QML_PACKAGES_ERR
    RESULT_VARIABLE PLAYER_QML_PACKAGES_RC
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(PLAYER_QML_PACKAGES_RC EQUAL 0 AND PLAYER_QML_PACKAGES)
    string(REPLACE "\n" ";" PLAYER_QML_PACKAGES "${PLAYER_QML_PACKAGES}")
    list(JOIN PLAYER_QML_PACKAGES ", " PLAYER_QML_DEPENDS)
    message(STATUS "Reverie: QML runtime packages resolved: ${PLAYER_QML_DEPENDS}")
else()
    set(PLAYER_QML_DEPENDS
        "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, \
qml6-module-qtquick-dialogs, qml6-module-qtquick-window, qml6-module-qtquick-templates, \
qml6-module-qtquick-shapes")
    message(WARNING "Reverie: could not resolve QML packages (${PLAYER_QML_PACKAGES_ERR}); "
                    "falling back to the static list")
endif()

# GStreamer decoders are plugins autoplugged by playbin3, and libqt6svg6 provides the qsvg
# image-format plugin the mark is drawn with - the binary does not link Qt6Svg at all. Without
# the decoders the application starts perfectly and then refuses to play anything, which is the
# worst failure to debug on someone else's machine.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "${PLAYER_QML_DEPENDS}, gstreamer1.0-plugins-base, gstreamer1.0-plugins-good, \
gstreamer1.0-plugins-bad, gstreamer1.0-libav, libqt6svg6")
# Not a hard dependency: the visualiser works on llvmpipe, it is just slower. See section 4.
# va-driver-all is what makes hardware decoding possible. The GStreamer side needs nothing
# added: the `va` plugin ships inside gstreamer1.0-plugins-bad, which is already a hard
# dependency, and it registers no elements at all on a machine with no capable device - so it
# costs nothing where it cannot be used. What is missing on such a machine is the libva driver.
#
# Recommends rather than Depends, because a machine with no GPU plays video perfectly well in
# software and should not be made to carry drivers it cannot use. Both target distros install
# these by default anyway, so for most people it is already satisfied; ~15 MB otherwise.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
    "gstreamer1.0-pulseaudio | gstreamer1.0-alsa, va-driver-all")

set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"Reverie plays music and video with visualisations rendered in the same
 window, rather than as a separate application. It has transport controls,
 one playlist, internet radio streams and a mini-player mode, and it does
 not manage a library.")

include(CPack)
