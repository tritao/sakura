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

SET (SAKURA_PROTOBUF_PREFIX "" CACHE PATH
	"Project-local protobuf/protobuf-c installation prefix")
FIND_PROGRAM (SAKURA_PROTOC_EXECUTABLE protoc
	HINTS "${SAKURA_PROTOBUF_PREFIX}/bin")
FIND_PROGRAM (SAKURA_PROTOC_C_EXECUTABLE protoc-c
	HINTS "${SAKURA_PROTOBUF_PREFIX}/bin")
IF (NOT SAKURA_PROTOC_EXECUTABLE OR NOT SAKURA_PROTOC_C_EXECUTABLE)
	MESSAGE (FATAL_ERROR
		"protoc and protoc-c are required. Run ./build.sh to fetch the project-local protobuf tools.")
ENDIF ()
pkg_check_modules (PROTOBUF_C REQUIRED IMPORTED_TARGET libprotobuf-c)

SET (SAKURA_PROTO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
SET (SAKURA_PROTO_GENERATED_C
	"${SAKURA_PROTO_GENERATED_DIR}/sakura/control.pb-c.c")
SET (SAKURA_PROTO_GENERATED_H
	"${SAKURA_PROTO_GENERATED_DIR}/sakura/control.pb-c.h")
ADD_CUSTOM_COMMAND (
	OUTPUT ${SAKURA_PROTO_GENERATED_C} ${SAKURA_PROTO_GENERATED_H}
	COMMAND ${CMAKE_COMMAND} -E make_directory "${SAKURA_PROTO_GENERATED_DIR}"
	COMMAND ${SAKURA_PROTOC_C_EXECUTABLE}
		--proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
		--c_out=${SAKURA_PROTO_GENERATED_DIR}
		"${CMAKE_CURRENT_SOURCE_DIR}/proto/sakura/control.proto"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/proto/sakura/control.proto"
	COMMENT "Generating protobuf-c control bindings"
	VERBATIM)
ADD_CUSTOM_TARGET (sakura-proto-generated
	DEPENDS ${SAKURA_PROTO_GENERATED_C} ${SAKURA_PROTO_GENERATED_H})

SET (SAKURA_CONTROL_SOURCES
	src/sakura-control-transport.c
	${SAKURA_PROTO_GENERATED_C})

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
	src/sakura-workspace-model.c
	src/sakura-workspace-persistence.c
	src/sakura-integrations.c
	src/sakura-startup.c
	${SAKURA_CONTROL_SOURCES}
	src/sakura-agent-client.c
	src/sakura-session.c)

ADD_EXECUTABLE (sakura)
TARGET_SOURCES (sakura PRIVATE
	src/sakura-main.c
	${SAKURA_APP_SOURCES})

TARGET_INCLUDE_DIRECTORIES (sakura PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${SAKURA_PROTO_GENERATED_DIR})
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

# GTK-free domain/session ownership. Keep this as a separately linkable target
# so the future agent can consume the same core boundary without pulling in
# GTK, VTE, X11, or the desktop application lifecycle.
ADD_LIBRARY (sakura-core STATIC
	src/sakura-core.c
	src/sakura-core-session.c
	src/sakura-core-workspace.c)
TARGET_INCLUDE_DIRECTORIES (sakura-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
TARGET_COMPILE_OPTIONS (sakura-core PRIVATE ${SAKURA_WARNING_FLAGS})
TARGET_LINK_LIBRARIES (sakura-core PUBLIC PkgConfig::GLIB m)

ADD_EXECUTABLE (sakura-agent
	src/sakura-agent.c
	${SAKURA_CONTROL_SOURCES})
TARGET_INCLUDE_DIRECTORIES (sakura-agent PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${SAKURA_PROTO_GENERATED_DIR})
TARGET_COMPILE_OPTIONS (sakura-agent PRIVATE ${SAKURA_WARNING_FLAGS})
TARGET_LINK_LIBRARIES (sakura-agent PRIVATE
	sakura-core
	PkgConfig::GLIB
	PkgConfig::GIO_UNIX
	PkgConfig::PROTOBUF_C)
ADD_DEPENDENCIES (sakura sakura-agent sakura-proto-generated)
ADD_DEPENDENCIES (sakura-agent sakura-proto-generated)
TARGET_COMPILE_DEFINITIONS (sakura PRIVATE
	SAKURA_AGENT_BUILD_PATH="${CMAKE_BINARY_DIR}/src/sakura-agent")

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
	sakura-core
	PkgConfig::GLIB
	PkgConfig::GIO_UNIX
	PkgConfig::GTK
	PkgConfig::VTE
	PkgConfig::X11
	PkgConfig::PROTOBUF_C
	m)

IF (SAKURA_ENABLE_SANITIZERS)
	TARGET_COMPILE_OPTIONS (sakura PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
	TARGET_LINK_OPTIONS (sakura PRIVATE -fsanitize=address,undefined)
ENDIF ()

IF (BUILD_TESTING)
	ADD_EXECUTABLE (sakura-core-model-test
		tests/test-sakura-core.c)
	TARGET_INCLUDE_DIRECTORIES (sakura-core-model-test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src)
	TARGET_COMPILE_OPTIONS (sakura-core-model-test PRIVATE
		${SAKURA_WARNING_FLAGS})
	TARGET_LINK_LIBRARIES (sakura-core-model-test PRIVATE
		sakura-core
		PkgConfig::GLIB)
	ADD_TEST (NAME core-model COMMAND sakura-core-model-test)

	ADD_EXECUTABLE (sakura-control-test
		tests/test-sakura-control.c
		${SAKURA_CONTROL_SOURCES})
	TARGET_INCLUDE_DIRECTORIES (sakura-control-test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${SAKURA_PROTO_GENERATED_DIR})
	TARGET_COMPILE_OPTIONS (sakura-control-test PRIVATE
		${SAKURA_WARNING_FLAGS})
	TARGET_COMPILE_DEFINITIONS (sakura-control-test PRIVATE
		SAKURA_AGENT_BUILD_PATH="${CMAKE_BINARY_DIR}/src/sakura-agent")
	TARGET_LINK_LIBRARIES (sakura-control-test PRIVATE
		sakura-core
		PkgConfig::GLIB
		PkgConfig::GIO_UNIX
		PkgConfig::PROTOBUF_C)
	ADD_DEPENDENCIES (sakura-control-test sakura-proto-generated)
	ADD_DEPENDENCIES (sakura-control-test sakura-agent)
	ADD_TEST (NAME control COMMAND sakura-control-test)

	ADD_EXECUTABLE (sakura-core-test)
	TARGET_SOURCES (sakura-core-test PRIVATE
		tests/test-core.c
		src/sakura-layout.c
		src/sakura-integrations.c)
	SET_PROPERTY (TARGET sakura-core-test APPEND PROPERTY COMPILE_DEFINITIONS SAKURA_CORE_TEST)
	TARGET_INCLUDE_DIRECTORIES (sakura-core-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	TARGET_COMPILE_OPTIONS (sakura-core-test PRIVATE ${SAKURA_WARNING_FLAGS})
	TARGET_LINK_LIBRARIES (sakura-core-test PRIVATE
		sakura-core
		PkgConfig::GLIB
		PkgConfig::GIO_UNIX
		PkgConfig::GTK
		PkgConfig::VTE
		m)
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
		${CMAKE_CURRENT_SOURCE_DIR}
		${SAKURA_PROTO_GENERATED_DIR})
	TARGET_COMPILE_DEFINITIONS (sakura-workspace-gtk-test PRIVATE
		VERSION="${VERSION}"
		DATADIR="${CMAKE_INSTALL_PREFIX}/share"
		SAKURA_SOURCE_ICON_DIR="${CMAKE_SOURCE_DIR}/icons"
		SAKURA_SOURCE_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/scripts"
		BUILDTYPE="${CMAKE_BUILD_TYPE}"
		SAKURA_AGENT_BUILD_PATH="${CMAKE_BINARY_DIR}/src/sakura-agent")
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
		sakura-core
		PkgConfig::GLIB
		PkgConfig::GIO_UNIX
		PkgConfig::GTK
		PkgConfig::VTE
		PkgConfig::X11
		PkgConfig::PROTOBUF_C
		m)
	ADD_DEPENDENCIES (sakura-workspace-gtk-test sakura-proto-generated)
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
	EXECUTE_PROCESS (
		COMMAND ${Python3_EXECUTABLE} -c "import Xlib"
		RESULT_VARIABLE PYTHON_XLIB_RESULT
		OUTPUT_QUIET ERROR_QUIET)
	IF (PYTHON_XLIB_RESULT EQUAL 0)
		SET (SAKURA_PYTHON_XLIB_FOUND TRUE)
	ELSE ()
		SET (SAKURA_PYTHON_XLIB_FOUND FALSE)
	ENDIF ()
	IF (BUILD_TESTING AND XVFB_RUN_EXECUTABLE)
		ADD_TEST (NAME workspace-gtk
		          COMMAND ${XVFB_RUN_EXECUTABLE} -a
		                  $<TARGET_FILE:sakura-workspace-gtk-test>)
		SET_TESTS_PROPERTIES (workspace-gtk PROPERTIES TIMEOUT 30)
	ENDIF (BUILD_TESTING AND XVFB_RUN_EXECUTABLE)
	IF (BUILD_TESTING AND XVFB_EXECUTABLE AND XDOTOOL_EXECUTABLE AND
	    SAKURA_PYTHON_XLIB_FOUND)
		# Keep packaged-process coverage for X11 input/startup/shutdown and the
		# dedicated drag test for real pointer-driven sidebar mutations.
		ADD_TEST (NAME ui-smoke
		          COMMAND ${Python3_EXECUTABLE}
		                  ${CMAKE_SOURCE_DIR}/tests/session-stress.py
		                  --binary $<TARGET_FILE:sakura>
		                  --iterations 1 --no-drag)
		SET_TESTS_PROPERTIES (ui-smoke PROPERTIES TIMEOUT 90)
		ADD_TEST (NAME ui-drag
		          COMMAND ${Python3_EXECUTABLE}
		                  ${CMAKE_SOURCE_DIR}/tests/session-stress.py
		                  --binary $<TARGET_FILE:sakura>
		                  --iterations 1 --drag-only)
		SET_TESTS_PROPERTIES (ui-drag PROPERTIES TIMEOUT 90)
	ENDIF (BUILD_TESTING AND XVFB_EXECUTABLE AND XDOTOOL_EXECUTABLE AND
	       SAKURA_PYTHON_XLIB_FOUND)
	IF (BUILD_TESTING AND XVFB_EXECUTABLE AND XDOTOOL_EXECUTABLE AND
	    NOT SAKURA_PYTHON_XLIB_FOUND)
		MESSAGE (WARNING "Skipping packaged UI tests: python-xlib is required by tests/session-stress.py")
	ENDIF ()
ENDIF (Python3_Interpreter_FOUND)

ADD_SUBDIRECTORY (po)

INSTALL (TARGETS sakura sakura-agent RUNTIME DESTINATION bin)
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
