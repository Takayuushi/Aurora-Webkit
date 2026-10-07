# This directory is added to WebKit's existing CMake build by
# cmake/WebKitProjectHook.cmake. It is intentionally not a standalone project:
# the WebKit build supplies the executable macros, generated headers, and
# framework targets used below.

if (NOT WIN32)
    message(FATAL_ERROR "Aurora's initial host application is Windows-only.")
endif ()

if (NOT COMMAND WEBKIT_EXECUTABLE_DECLARE OR NOT COMMAND WEBKIT_EXECUTABLE)
    message(FATAL_ERROR
        "Aurora must be added from WebKit's CMake build so its target helpers are available.")
endif ()

if (NOT TARGET WebKit)
    message(FATAL_ERROR
        "Aurora must be added after WebKit's CMake targets have been declared.")
endif ()

set(Aurora_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)

# Use the same WebKit framework targets as the upstream Windows MiniBrowser.
# WebKit's CMake helpers resolve the corresponding targets and include paths;
# do not link hand-picked .lib files here.
set(Aurora_FRAMEWORKS
    JavaScriptCore
    PAL
    WTF
    WebCore
    WebKit
)

set(Aurora_PRIVATE_INCLUDE_DIRECTORIES
    "${CMAKE_BINARY_DIR}"
)

set(Aurora_PRIVATE_DEFINITIONS
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set(Aurora_PRIVATE_LIBRARIES
    user32
)

WEBKIT_EXECUTABLE_DECLARE(Aurora)
WEBKIT_EXECUTABLE(Aurora)

set_target_properties(Aurora PROPERTIES
    WIN32_EXECUTABLE ON
)
