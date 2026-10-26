# Aurora 1.0 — Release Notes

## Overview
Aurora 1.0 is the first stable-channel baseline for the native Windows browser shell built on WebKit. It keeps Aurora's own branding and is not Chromium-based.

## Highlights
- Safari-inspired browser chrome, tab strip, and tab overview.
- Tab selection and overview transitions with substantially improved animation behavior.
- Navigation controls, address/search bar, sidebar, bookmarks, Favorites, Reading List, and browsing history.
- Downloads menu and full Downloads page with progress/status and Show in Folder.
- Light, Dark, and Automatic appearance settings.
- Find in Page, Reader Mode, page zoom, and local JavaScript user scripts.
- Native Windows packaging through the repository's GitHub Actions workflow.

## Known limitations
- Some tab-overview thumbnails may show the generic Aurora start page instead of the live page for a tab.
- A brief low-resolution preview may appear during the start-page tab-selection transition.
- Recent rendering transitions still require regression testing for intermittent blank or black frames.
- Downloads are not restored after restarting Aurora.
- Some Apple-service-dependent Safari features are not available on Windows; see README for details.

## Validation
This release establishes the stable-channel source baseline. A successful Windows build and hands-on regression testing should be confirmed against the packaged artifact before describing the release as fully verified.
