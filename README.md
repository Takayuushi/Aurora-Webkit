# Aurora

Aurora is a native Windows browser shell built on the public **WebKit Windows C API**.

The project is **not Chromium-based**. Aurora keeps its own browser UI and application logic in this repository and connects that code to an upstream WebKit Windows build at build time.

## Current status

Aurora has progressed beyond the original proof-of-concept window and now includes a custom Safari-inspired browser interface plus a growing set of browser services.

### Browser UI

- Custom Windows chrome with Aurora branding
- Tab strip with per-tab titles and favicons
- New-tab button and tab closing
- Back and forward navigation
- Sidebar control
- Smart Search/address field
- Privacy/shield control
- Reader Mode control
- Reload control
- Share control
- Downloads control
- Main browser menu
- Custom window controls and resize handling
- DPI-aware layout and browser content resizing

The UI is designed to follow the visual language and compact proportions of modern Safari while using Aurora's own branding and original interface artwork.

## Browser services

### History

Aurora records HTTP/HTTPS visits locally and provides:

- History view
- Visit counts
- Recent-history ordering
- Clear History
- Sidebar history section

History data is stored under Aurora's local application-data directory.

### Bookmarks

Aurora provides:

- Bookmark This Page
- Remove Bookmark
- Bookmark state in the menu
- Persistent bookmark storage
- Bookmark list page
- Sidebar bookmarks section

### Reading List

Aurora provides:

- Add page to Reading List
- Persistent Reading List storage
- Reading List page
- Sidebar Reading List section

### Downloads

Aurora uses WebKit's download callbacks and currently provides:

- Download destination handling
- Download progress tracking
- Download completion/failure state
- Recent-download popover from the toolbar
- Show All Downloads
- Full Downloads page
- Show-in-Folder support for downloaded files

The toolbar download control is intended to behave like a browser download popover rather than simply opening the Windows Downloads directory.

### Reader Mode

Reader Mode currently uses page-level WebKit JavaScript/CSS injection to provide a simplified reading presentation. It is an early implementation and is not yet a full article-extraction system.

## URL handling

The address bar accepts:

- Full URLs such as `https://www.youtube.com`
- Bare domains such as `youtube.com`
- Search text

For bare domains, Aurora prefers a `www.` HTTPS variant when appropriate and retains a fallback URL for failed provisional navigation.

## Project layout

- `src/main.cpp` — Aurora's Win32 window, browser UI, tab management, navigation, history, bookmarks, Reading List, downloads, Reader Mode, and WebKit API integration.
- `CMakeLists.txt` — declares the Aurora executable inside WebKit's existing CMake graph.
- `cmake/WebKitProjectHook.cmake` — connects Aurora to an upstream WebKit CMake configure without modifying the WebKit source tree.
- `Resources/` — Aurora branding assets such as the emblem and wordmark SVGs.
- `.github/workflows/build-aurora.yml` — Windows GitHub Actions build that checks out/builds upstream WebKit and then builds/packages Aurora.

## Building on GitHub Actions

Aurora is currently built on a GitHub-hosted Windows runner.

The workflow:

1. Checks out Aurora.
2. Resolves an upstream WebKit revision.
3. Restores or creates the cached WebKit source, Windows libraries, and build output.
4. Configures WebKit with Aurora's CMake hook.
5. Builds the WebKit Windows Release graph.
6. Builds the `Aurora` target.
7. Packages Aurora together with the WebKit companion processes, runtime DLLs, and resources.
8. Uploads an `aurora-windows-x64` artifact.

The workflow is manually triggered with `workflow_dispatch`.

Because the WebKit build is performed on GitHub's hosted Windows runner, shutting down the local PC does not stop a running Actions job.

## Runtime packaging

Aurora depends on the matching WebKit Windows runtime produced by the same build.

The package therefore includes the required WebKit companion processes, DLLs, and resource files alongside `Aurora.exe`.

Aurora does not vendor the upstream WebKit source into this repository.

## What is implemented vs. planned

Implemented today:

- Safari-inspired custom browser chrome
- Tabs and favicons
- Navigation
- Address/search handling
- History
- Bookmarks
- Reading List
- Downloads and download progress
- Reader Mode
- Sidebar for bookmarks, Reading List, and history
- Browser menu
- Page zoom controls
- Custom window sizing/maximize behavior
- Aurora branding

Still planned or being expanded:

- Full private browsing/data-store isolation
- Multiple browser windows
- Tab groups
- Profiles
- Full extension management and extension APIs
- Translation
- Fully featured Find in Page
- Printing integration
- Save Page As integration
- More complete share behavior
- Richer download management
- More complete Reader article extraction
- Further visual refinement toward the latest Safari reference

Some menu entries are therefore still placeholders while their underlying browser-core integrations are being implemented.

## Design direction

Aurora aims for a clean, compact, lightweight browser experience inspired by the feel and proportions of modern Safari.

The project uses:

- Aurora's own logo and wordmark
- Original UI drawings
- Generic browser interaction patterns
- WebKit as the browser engine

Apple trademarks, logos, and proprietary Safari assets are not used as Aurora branding.

## License / upstream relationship

Aurora source code in this repository is separate from upstream WebKit.

WebKit remains an external dependency and is obtained from the upstream WebKit repository during the build process.

