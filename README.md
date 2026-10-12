# Aurora

Aurora is a native Windows browser shell built on WebKit's Windows C API. It is **not Chromium-based**.

The Windows interface follows the layout and interaction patterns of modern Safari while keeping Aurora’s own branding and a native Win32/WebKit implementation. The menu bar uses Aurora naming and its own lowercase “a” mark rather than Apple’s logo or Safari product labels.

## Available browser features

### Browser window and navigation

- Safari-inspired tab strip with page titles, favicons, close controls, and a new-tab control.
- Back, forward, reload/stop, address/search, sidebar, share, downloads, and browser-menu controls.
- Multiple browser windows.
- New Private Window using a separate non-persistent WebKit website data store.
- DPI-aware window sizing, custom title-bar handling, and resize/maximize controls.
- A Bookmarks / Reading List / History sidebar. Opening or closing it resizes and repaints the WebKit content viewport.
- Address-bar URL detection and search, with Google, Bing, or DuckDuckGo selectable in Settings.
- Page zoom in, zoom out, and reset.
- Basic Reader Mode using page-level JavaScript/CSS injection.

### History, Favorites, and Reading List

- HTTP/HTTPS visits are stored locally with titles, visit counts, and visit timestamps.
- History page, history sidebar, and clear-history controls.
- Persistent bookmarks/Favorites, remove-bookmark behavior, and a Favorites section on the start page.
- Persistent Reading List, with separate Add to Reading List and Show Reading List actions.
- Local data files live under %LOCALAPPDATA%\Aurora.

Private windows use a non-persistent website data store and skip adding visits to persistent browsing history. Bookmarks and Reading List remain available because these are user-saved items rather than session history.

### Downloads, sharing, and page actions

- An always-visible Downloads toolbar control that opens a recent-downloads menu and a full Downloads page.
- WebKit download callbacks, suggested filenames, progress/completion/failure status, and Show in Folder. Selecting a completed item opens the downloaded file; an unfinished item reveals its destination in File Explorer when the file is present.
- Download entries are currently kept in memory for the running browser window; the download list is not restored after restarting Aurora.
- Share menu with Copy Link, Email Link, and Add Bookmark.
- Save Page As exports an MHTML web archive where supported by the current WebKit build.
- Print opens the current HTTP/HTTPS page in the default browser after confirmation, where Windows' native Ctrl+P print flow is available. This fallback is used because the pinned Windows WebKit port does not export its page-to-PDF drawing API; the default browser may not share Aurora's login session.
- Ctrl+F Find in Page with previous/next controls and match highlighting.

### Appearance and settings

- Light, Dark, and Automatic (follow Windows app theme); theme selection persists between runs.
- Settings for home page, default search engine, and downloads folder.
- Website preferences for JavaScript, automatic image loading, automatic pop-up windows, local storage, and Web Inspector/developer extras.
- Clear History and Clear Website Data controls with confirmation.

### Extensions

Aurora currently supports **local JavaScript user scripts**, not the Safari Web Extensions format. Place trusted .js files in:

%LOCALAPPDATA%\Aurora\extensions

Use **Extensions → Open Extensions Folder**, then reload a web page. Scripts are injected into regular HTTP/HTTPS top-level pages and are disabled in Private Browsing. This facility does not provide Safari's full extension API, permission model, or extension store.

## Apple-specific services

Features that depend on Apple's services, Apple IDs, or macOS-only frameworks may remain placeholders or require Windows equivalents—for example, iCloud Tabs/Keychain sync, Handoff/Continuity, AirDrop, and Apple's extension distribution/integration. The rest of the browser UI and ordinary browser functions are being implemented as native Windows/WebKit equivalents rather than left as placeholders.

## Repository layout

- src/main.cpp — Win32 browser shell, toolbar, tabs, browser services, built-in pages, settings, and WebKit integration.
- CMakeLists.txt — declares Aurora inside WebKit's existing CMake graph.
- cmake/WebKitProjectHook.cmake — connects Aurora to the upstream WebKit configure.
- .github/workflows/build-aurora.yml — Windows GitHub Actions build and runtime-packaging workflow.

## Build

The workflow is manually triggered with workflow_dispatch. It checks out a WebKit revision, restores or builds WebKit's Windows dependencies/build graph, builds Aurora, packages the matching WebKit runtime, and uploads the aurora-windows-x64 artifact.

WebKit is not vendored into this repository. The packaged executable must remain with the WebKit helper processes, libraries, and resources produced by the same build.

## Current limitations

This is a native browser shell, not yet a full one-to-one Safari replacement. The local user-script feature is not Safari Web Extensions; Reader Mode is a simple style-based implementation; and Print currently opens the page in the default browser because the pinned Windows WebKit build does not expose its native PDF-rendering API. The source and runtime still need a successful Windows build and hands-on regression testing after these recent changes.
