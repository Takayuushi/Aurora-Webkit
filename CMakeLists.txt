# This file is included by cmake/WebKitProjectHook.cmake during WebKit's
# project() call, before WebKit's targets and helper macros exist. Keep Aurora
# source files in this repository and defer only target creation/setup until
# the end of WebKit's top-level configure.

if (NOT WIN32)
    message(FATAL_ERROR "Aurora's initial host application is Windows-only.")
endif ()

function(_aurora_finish_webkit_target)
    if (NOT COMMAND WEBKIT_EXECUTABLE OR NOT TARGET WebKit)
        message(FATAL_ERROR
            "Aurora's deferred target setup did not run after WebKit targets and helpers were defined.")
    endif ()

    set(Aurora_SOURCES
        "${AURORA_SOURCE_DIR}/src/main.cpp"
    )

    # Use the same WebKit framework targets as the upstream Windows MiniBrowser.
    # WebKit's helper resolves their generated include paths and link targets;
    # Aurora does not name or list individual .lib files.
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
        shell32
        urlmon
    )

    WEBKIT_EXECUTABLE_DECLARE(Aurora)
    WEBKIT_EXECUTABLE(Aurora)

    set_target_properties(Aurora PROPERTIES
        WIN32_EXECUTABLE ON
    )
endfunction()

# WebKit's root CMakeLists includes its common helpers and adds Source after
# project() returns. Create Aurora after those targets and helpers are ready.
cmake_language(DEFER
    DIRECTORY "${CMAKE_SOURCE_DIR}"
    CALL _aurora_finish_webkit_target
)
