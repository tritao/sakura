CMAKE_MINIMUM_REQUIRED (VERSION 3.15 FATAL_ERROR)
CMAKE_POLICY(SET CMP0005 NEW)
PROJECT (sakura)
SET (AUTHOR "David Gómez Espinosa" INTERNAL "Author")
SET (VERSION "3.8.9")


INCLUDE (FindPkgConfig)
INCLUDE (CTest)
FIND_PACKAGE (Python3 COMPONENTS Interpreter)

OPTION (SAKURA_ENABLE_SANITIZERS
	"Build Sakura and the core tests with AddressSanitizer and UBSan"
	OFF)

IF (NOT PKG_CONFIG_FOUND)
	MESSAGE (FATAL_ERROR "pkg-config not found...")
ENDIF (NOT PKG_CONFIG_FOUND)

pkg_check_modules (GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.40)
IF (NOT GLIB_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have glib >= 2.40 development libraries installed...")
ENDIF (NOT GLIB_FOUND)

pkg_check_modules (GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)
IF (NOT GIO_UNIX_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have glib >= 2.40 development libraries installed...")
ENDIF (NOT GIO_UNIX_FOUND)

pkg_check_modules (GTK REQUIRED IMPORTED_TARGET gtk+-3.0>=3.20)
IF (NOT GTK_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have gtk >= 3.20 development libraries installed...")
ENDIF (NOT GTK_FOUND)

pkg_check_modules (VTE REQUIRED IMPORTED_TARGET vte-2.91>=0.50)
IF (NOT VTE_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have vte >= 0.50 development libraries installed...")
ENDIF (NOT VTE_FOUND)

# WebKitGTK is optional. When available, PR tabs render the GitHub page
# inside Sakura; otherwise they retain the terminal-based gh pr view fallback.
pkg_check_modules (WEBKIT41 QUIET IMPORTED_TARGET webkit2gtk-4.1)
IF (NOT WEBKIT41_FOUND)
	pkg_check_modules (WEBKIT40 QUIET IMPORTED_TARGET webkit2gtk-4.0)
ENDIF ()

pkg_check_modules (X11 REQUIRED IMPORTED_TARGET x11)
IF (NOT X11_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have x11 development libraries installed...")
ENDIF (NOT X11_FOUND)

FIND_PROGRAM(POD2MAN pod2man)
MESSAGE ("pod2man executable is" ${POD2MAN})

SET (SAKURA_APP_SOURCES
	src/sakura.c
	src/sakura-layout.c
	src/sakura-tab.c
	src/sakura-workspace.c
	src/sakura-integrations.c
	src/sakura-session.c)

ADD_EXECUTABLE (sakura)
TARGET_SOURCES (sakura PRIVATE
	src/sakura-main.c
	${SAKURA_APP_SOURCES})

TARGET_INCLUDE_DIRECTORIES (sakura PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
TARGET_COMPILE_DEFINITIONS (sakura PRIVATE
	VERSION="${VERSION}"
	DATADIR="${CMAKE_INSTALL_PREFIX}/share"
	SAKURA_SOURCE_ICON_DIR="${CMAKE_SOURCE_DIR}/icons"
	SAKURA_SOURCE_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/scripts"
	BUILDTYPE="${CMAKE_BUILD_TYPE}")
IF (WEBKIT41_FOUND OR WEBKIT40_FOUND)
	TARGET_COMPILE_DEFINITIONS (sakura PRIVATE HAVE_WEBKITGTK)
ENDIF ()

# GTK and GLib callbacks intentionally leave some ABI parameters unused. The
# legacy configuration stores accelerator/button values as gint while GDK
# exposes event masks/buttons as guint; keep those compatibility warnings out
# of every target while retaining the main warning policy.
SET (SAKURA_WARNING_FLAGS
	-Wall -Wextra -Wformat=2
	-Wno-unused-parameter -Wno-sign-compare)
IF (CMAKE_BUILD_TYPE MATCHES "Debug")
	TARGET_COMPILE_OPTIONS (sakura PRIVATE ${SAKURA_WARNING_FLAGS})
ELSE ()
	TARGET_COMPILE_OPTIONS (sakura PRIVATE ${SAKURA_WARNING_FLAGS}
		-O2 -Wno-deprecated-declarations)
ENDIF ()
IF (WEBKIT41_FOUND)
	TARGET_LINK_LIBRARIES (sakura PRIVATE PkgConfig::WEBKIT41)
ELSEIF (WEBKIT40_FOUND)
	TARGET_LINK_LIBRARIES (sakura PRIVATE PkgConfig::WEBKIT40)
ENDIF ()
TARGET_LINK_LIBRARIES (sakura PRIVATE
	PkgConfig::GLIB
	PkgConfig::GIO_UNIX
	PkgConfig::GTK
	PkgConfig::VTE
	PkgConfig::X11
	m)

IF (SAKURA_ENABLE_SANITIZERS)
	TARGET_COMPILE_OPTIONS (sakura PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	TARGET_LINK_OPTIONS (sakura PRIVATE -fsanitize=address,undefined)
ENDIF ()

IF (BUILD_TESTING)
	ADD_EXECUTABLE (sakura-core-test)
	TARGET_SOURCES (sakura-core-test PRIVATE
		tests/test-core.c
		src/sakura-layout.c
		src/sakura-integrations.c
		src/sakura-session.c)
	SET_PROPERTY (TARGET sakura-core-test APPEND PROPERTY COMPILE_DEFINITIONS SAKURA_CORE_TEST)
	TARGET_INCLUDE_DIRECTORIES (sakura-core-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	TARGET_COMPILE_OPTIONS (sakura-core-test PRIVATE ${SAKURA_WARNING_FLAGS})
	TARGET_LINK_LIBRARIES (sakura-core-test PRIVATE
		PkgConfig::GLIB
		PkgConfig::GIO_UNIX
		PkgConfig::GTK
		PkgConfig::VTE)
	IF (SAKURA_ENABLE_SANITIZERS)
		TARGET_COMPILE_OPTIONS (sakura-core-test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
		TARGET_LINK_OPTIONS (sakura-core-test PRIVATE -fsanitize=address,undefined)
	ENDIF ()
	ADD_TEST (NAME core COMMAND sakura-core-test)

	ADD_EXECUTABLE (sakura-workspace-gtk-test)
	TARGET_SOURCES (sakura-workspace-gtk-test PRIVATE
		tests/test-workspace-gtk.c
		${SAKURA_APP_SOURCES})
	TARGET_INCLUDE_DIRECTORIES (sakura-workspace-gtk-test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR})
	TARGET_COMPILE_DEFINITIONS (sakura-workspace-gtk-test PRIVATE
		VERSION="${VERSION}"
		DATADIR="${CMAKE_INSTALL_PREFIX}/share"
		SAKURA_SOURCE_ICON_DIR="${CMAKE_SOURCE_DIR}/icons"
		SAKURA_SOURCE_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/scripts"
		BUILDTYPE="${CMAKE_BUILD_TYPE}")
	TARGET_COMPILE_OPTIONS (sakura-workspace-gtk-test PRIVATE
		${SAKURA_WARNING_FLAGS})
	IF (WEBKIT41_FOUND)
		TARGET_COMPILE_DEFINITIONS (sakura-workspace-gtk-test PRIVATE HAVE_WEBKITGTK)
		TARGET_LINK_LIBRARIES (sakura-workspace-gtk-test PRIVATE PkgConfig::WEBKIT41)
	ELSEIF (WEBKIT40_FOUND)
		TARGET_COMPILE_DEFINITIONS (sakura-workspace-gtk-test PRIVATE HAVE_WEBKITGTK)
		TARGET_LINK_LIBRARIES (sakura-workspace-gtk-test PRIVATE PkgConfig::WEBKIT40)
	ENDIF ()
	TARGET_LINK_LIBRARIES (sakura-workspace-gtk-test PRIVATE
		PkgConfig::GLIB
		PkgConfig::GIO_UNIX
		PkgConfig::GTK
		PkgConfig::VTE
		PkgConfig::X11
		m)
	IF (SAKURA_ENABLE_SANITIZERS)
		TARGET_COMPILE_OPTIONS (sakura-workspace-gtk-test PRIVATE
			-fsanitize=address,undefined -fno-omit-frame-pointer)
		TARGET_LINK_OPTIONS (sakura-workspace-gtk-test PRIVATE
			-fsanitize=address,undefined)
	ENDIF ()
ENDIF (BUILD_TESTING)

IF (Python3_Interpreter_FOUND)
	ADD_CUSTOM_TARGET (session-stress
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/session-stress.py
		        --binary $<TARGET_FILE:sakura>
		DEPENDS sakura
		USES_TERMINAL)

	FIND_PROGRAM (XVFB_EXECUTABLE Xvfb)
	FIND_PROGRAM (XVFB_RUN_EXECUTABLE xvfb-run)
	FIND_PROGRAM (XDOTOOL_EXECUTABLE xdotool)
	IF (BUILD_TESTING AND XVFB_RUN_EXECUTABLE)
		ADD_TEST (NAME workspace-gtk
		          COMMAND ${XVFB_RUN_EXECUTABLE} -a
		                  $<TARGET_FILE:sakura-workspace-gtk-test>)
		SET_TESTS_PROPERTIES (workspace-gtk PROPERTIES TIMEOUT 30)
	ENDIF (BUILD_TESTING AND XVFB_RUN_EXECUTABLE)
	IF (BUILD_TESTING AND XVFB_EXECUTABLE AND XDOTOOL_EXECUTABLE)
		# Keep one packaged-process smoke test for X11 input/startup/shutdown.
		# Detailed workspace behavior belongs to the in-process GTK target.
		ADD_TEST (NAME ui-smoke
		          COMMAND ${Python3_EXECUTABLE}
		                  ${CMAKE_SOURCE_DIR}/tests/session-stress.py
		                  --binary $<TARGET_FILE:sakura>
		                  --iterations 1 --no-drag)
		SET_TESTS_PROPERTIES (ui-smoke PROPERTIES TIMEOUT 90)
	ENDIF (BUILD_TESTING AND XVFB_EXECUTABLE AND XDOTOOL_EXECUTABLE)
ENDIF (Python3_Interpreter_FOUND)

ADD_SUBDIRECTORY (po)

INSTALL (TARGETS sakura RUNTIME DESTINATION bin)
INSTALL (FILES sakura.desktop DESTINATION share/applications)
INSTALL (FILES terminal-tango.svg DESTINATION share/pixmaps)
INSTALL (FILES
	icons/hicolor/scalable/apps/sakura-codex.svg
	icons/hicolor/scalable/apps/git.svg
	icons/hicolor/scalable/apps/github.svg
	DESTINATION share/icons/hicolor/scalable/apps)
IF (POD2MAN)
	INSTALL (FILES ${sakura_BINARY_DIR}/sakura.1 DESTINATION share/man/man1)
ENDIF (POD2MAN)
INSTALL (FILES README.md DESTINATION share/doc/sakura)
INSTALL (PROGRAMS scripts/sakura-codex-session-hook scripts/sakura-codex-session-name DESTINATION bin)

FILE (GLOB MO_FILES po/*.mo)

SET_DIRECTORY_PROPERTIES (PROPERTIES
	ADDITIONAL_MAKE_CLEAN_FILES "${MO_FILES};po/sakura.pot")
SET (EXECUTABLE_OUTPUT_PATH src)

ADD_CUSTOM_TARGET (distclean
	rm -f Makefile cmake_install.cmake CMakeCache.txt install_manifest.txt sakura.1
	COMMAND rm -f po/*.mo po/sakura.pot po/Makefile po/cmake_install.cmake
	COMMAND rm -rf CMakeFiles po/CMakeFiles)

IF (POD2MAN)
ADD_CUSTOM_TARGET(man ALL
	COMMAND ${POD2MAN} -u --section=1 --release=${VERSION} --center '' ${sakura_SOURCE_DIR}/sakura.pod sakura.1)
ENDIF (POD2MAN)
