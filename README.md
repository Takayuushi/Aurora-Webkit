# Aurora

Aurora is a small native Windows host for the public WebKit Windows C API. Its
first milestone creates one Win32 window, embeds a WKView, and loads
https://www.google.com. The Aurora sources are kept in this repository; they
are not copied into or committed with upstream WebKit.

## Project layout

- src/main.cpp creates the Win32 window, WebKit context and page configuration,
  embeds the WebKit view, resizes it with the window, and loads the initial URL.
- CMakeLists.txt declares the Aurora executable using WebKit's own CMake
  executable helper and framework targets.
- cmake/WebKitProjectHook.cmake connects this folder to a WebKit CMake
  configure without editing the upstream WebKit source tree.

This is an integration target, not a standalone CMake build. It expects to be
added to a Windows WebKit build where WebKit's CMake macros and targets already
exist. It deliberately uses the same framework target list as the upstream
Windows MiniBrowser rather than naming .lib files.

## Connecting it to a WebKit build

In a future Windows GitHub Actions job, check out both repositories side by
side. From the upstream WebKit checkout, pass the Aurora hook and source path
to build-webkit:

    perl Tools/Scripts/build-webkit --release --cmakeargs="-DCMAKE_PROJECT_INCLUDE:FILEPATH=D:/a/work/Aurora/cmake/WebKitProjectHook.cmake -DAURORA_SOURCE_DIR:PATH=D:/a/work/Aurora"

Replace D:/a/work/Aurora with the actual Aurora checkout path used by that
job. The hook waits until WebKit's top-level CMake directory has declared its
targets, then adds Aurora as a separate CMake subdirectory. Aurora's target
links through WebKit's JavaScriptCore, PAL, WTF, WebCore, and WebKit
targets and their build-provided include paths.

The build must use a matching Windows WebKit build and runtime output. The
application still needs WebKit's companion process executables, DLLs, and
resources at runtime; this project does not package or download those files.

## Current scope

The first window has no toolbar, tabs, bookmarks, or other browser UI. Those
can be added after the WebKit view builds and runs in the Actions environment.
