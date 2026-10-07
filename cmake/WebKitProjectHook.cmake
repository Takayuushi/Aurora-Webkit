# Pass this file to WebKit's CMake configure with CMAKE_PROJECT_INCLUDE.
# WebKit includes it from the top-level project() call, before its framework
# targets and executable helper macros are defined. Include Aurora's separate
# CMake file now; it defers only target creation/setup until WebKit's configure
# ends. This avoids adding a CMake subdirectory from deferred execution.

if (NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    return ()
endif ()

# Ignore other CMake projects that happen to use the same project hook.
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/Source/WebKit/CMakeLists.txt")
    return ()
endif ()

if (NOT DEFINED AURORA_SOURCE_DIR OR AURORA_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR
        "Set AURORA_SOURCE_DIR to the Aurora checkout when enabling the Aurora WebKit project hook.")
endif ()

if (NOT IS_ABSOLUTE "${AURORA_SOURCE_DIR}")
    message(FATAL_ERROR
        "AURORA_SOURCE_DIR must be an absolute path: ${AURORA_SOURCE_DIR}")
endif ()

if (NOT EXISTS "${AURORA_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "AURORA_SOURCE_DIR does not contain CMakeLists.txt: ${AURORA_SOURCE_DIR}")
endif ()

get_property(_aurora_hook_already_registered
    DIRECTORY PROPERTY AURORA_WEBKIT_PROJECT_HOOK_REGISTERED
)
if (_aurora_hook_already_registered)
    return ()
endif ()
set_property(DIRECTORY PROPERTY AURORA_WEBKIT_PROJECT_HOOK_REGISTERED TRUE)

include("${AURORA_SOURCE_DIR}/CMakeLists.txt")
