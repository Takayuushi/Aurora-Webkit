# Aurora 1.0 — Release Notes

## Overview
Aurora 1.0 establishes the first stable-channel baseline for the native Windows browser shell built on WebKit. It retains Aurora's own branding and is not Chromium-based.

## Highlights
- Safari-inspired browser chrome, tab strip, and tab overview.
- Improved tab overview activation and tab-selection transitions.
- Navigation controls, address/search bar, sidebar, bookmarks, Favorites, Reading List, and browsing history.
- Downloads menu and full Downloads page with progress/status and Show in Folder.
- Light, Dark, and Automatic appearance settings.
- Find in Page, Reader Mode, page zoom, and local JavaScript user scripts.
- Windows build and packaging workflow.

## Known limitations
- Some tab-overview thumbnails may show the generic Aurora start page instead of the live page for a tab.
- A brief low-resolution preview may appear during the start-page tab-selection transition.
- Intermittent blank or black frames still need hands-on regression testing.
- Downloads are not restored after restarting Aurora.
- Some Apple-service-dependent Safari features are unavailable on Windows; see README for details.

## Validation
- GitHub Actions workflow run **#196** — **build-aurora-windows #196** — completed successfully (green check) for commit [7cee018](https://github.com/Takayuushi/Aurora-Webkit/commit/7cee01894b63a0a03f9ef390b2b6a892839251c1), “Avoid blocking overview grid paint during activation,” on `dev/aurora-ui-improvements`.
- A successful CI run confirms the workflow passed; it does not replace hands-on testing of the packaged browser and the known preview/rendering issues above.
