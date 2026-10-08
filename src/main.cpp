#include <windows.h>
#include <windowsx.h>

#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageStateClient.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKRetainPtr.h>
#include <WebKit/WKString.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr wchar_t windowClassName[] = L"AuroraWindow";
constexpr wchar_t windowTitle[] = L"Aurora";

constexpr UINT kCommandBack = 1001;
constexpr UINT kCommandForward = 1002;
constexpr UINT kCommandReload = 1003;
constexpr UINT kCommandSidebar = 1004;
constexpr UINT kCommandNewTab = 1005;
constexpr UINT kCommandMenu = 1006;
constexpr UINT kCommandAddress = 1007;
constexpr UINT kCommandCloseTab = 1008;

constexpr UINT kMenuNewTab = 2001;
constexpr UINT kMenuNewWindow = 2002;
constexpr UINT kMenuBack = 2003;
constexpr UINT kMenuForward = 2004;
constexpr UINT kMenuReload = 2005;
constexpr UINT kMenuZoomIn = 2006;
constexpr UINT kMenuZoomOut = 2007;
constexpr UINT kMenuResetZoom = 2008;
constexpr UINT kMenuAbout = 2009;
constexpr UINT kMenuQuit = 2010;

constexpr int kTabBarHeight = 40;
constexpr int kToolbarHeight = 54;
constexpr int kToolbarHorizontalPadding = 14;
constexpr int kToolbarButtonSize = 36;
constexpr int kToolbarGap = 7;
constexpr int kLogoSize = 25;
constexpr int kTabMinWidth = 150;
constexpr int kTabMaxWidth = 260;

struct BrowserState;
struct TabState;

void didChangeIsLoading(const void*);
void didChangeTitle(const void*);
void didChangeActiveURL(const void*);
void didChangeEstimatedProgress(const void*);
void didChangeCanGoBack(const void*);
void didChangeCanGoForward(const void*);

std::wstring createString(WKStringRef string)
{
    if (!string)
        return { };

    size_t maximumSize = WKStringGetMaximumUTF8CStringSize(string);
    if (!maximumSize)
        return { };

    std::string utf8(maximumSize, '\0');
    WKStringGetUTF8CString(string, utf8.data(), maximumSize);
    if (!utf8.empty() && utf8.back() == '\0')
        utf8.pop_back();

    if (utf8.empty())
        return { };

    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0)
        return { };

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(), length);
    return result;
}

std::wstring createString(WKURLRef url)
{
    if (!url)
        return { };

    auto string = adoptWK(WKURLCopyString(url));
    return createString(string.get());
}

std::string toUTF8(const std::wstring& value)
{
    if (value.empty())
        return { };

    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return { };

    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

WKRetainPtr<WKURLRef> createWKURL(const std::wstring& value)
{
    auto utf8 = toUTF8(value);
    return adoptWK(WKURLCreateWithUTF8CString(utf8.c_str()));
}

std::wstring percentEncode(const std::wstring& value)
{
    auto utf8 = toUTF8(value);
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;

    for (unsigned char byte : utf8) {
        if ((byte >= 'a' && byte <= 'z')
            || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9')
            || byte == '-'
            || byte == '_'
            || byte == '.'
            || byte == '~') {
            encoded.push_back(static_cast<char>(byte));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(hex[(byte >> 4) & 0x0F]);
        encoded.push_back(hex[byte & 0x0F]);
    }

    int length = MultiByteToWideChar(CP_UTF8, 0, encoded.data(), static_cast<int>(encoded.size()), nullptr, 0);
    if (length <= 0)
        return { };

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, encoded.data(), static_cast<int>(encoded.size()), result.data(), length);
    return result;
}

void enableDpiAwareness()
{
    using SetProcessDpiAwarenessContextFunction = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);

    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (!user32)
        return;

    auto setProcessDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext")
    );

    if (setProcessDpiAwarenessContext && setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return;

    SetProcessDPIAware();
}

UINT windowDpi(HWND window)
{
    using GetDpiForWindowFunction = UINT (WINAPI*)(HWND);
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
            GetProcAddress(user32, "GetDpiForWindow")
        );
        if (getDpiForWindow) {
            UINT dpi = getDpiForWindow(window);
            if (dpi)
                return dpi;
        }
    }

    HDC dc = GetDC(window);
    UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc)
        ReleaseDC(window, dc);
    return dpi ? dpi : 96;
}

int scaleForDpi(HWND window, int logicalPixels)
{
    return MulDiv(logicalPixels, static_cast<int>(windowDpi(window)), 96);
}

void drawAuroraMark(HDC dc, const RECT& rect, bool darkBackground)
{
    RECT circle = rect;
    int width = circle.right - circle.left;
    int height = circle.bottom - circle.top;
    int size = std::min(width, height);
    circle.right = circle.left + size;
    circle.bottom = circle.top + size;

    HBRUSH outerBrush = CreateSolidBrush(darkBackground ? RGB(22, 44, 64) : RGB(47, 184, 169));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, outerBrush));
    Ellipse(dc, circle.left, circle.top, circle.right, circle.bottom);
    SelectObject(dc, oldBrush);
    DeleteObject(outerBrush);

    // Layered ribbon bands: an original Aurora placeholder mark, deliberately not based on
    // any third-party browser logo.
    HPEN tealPen = CreatePen(PS_SOLID, std::max(1, size / 8), darkBackground ? RGB(84, 230, 213) : RGB(17, 113, 132));
    HPEN mintPen = CreatePen(PS_SOLID, std::max(1, size / 10), darkBackground ? RGB(165, 255, 184) : RGB(158, 239, 173));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, tealPen));

    int left = circle.left + size / 7;
    int top = circle.top + size / 7;
    int right = circle.right - size / 7;
    int bottom = circle.bottom - size / 7;

    Arc(dc, left, top, right, bottom, right, top + size / 4, left + size / 5, bottom);
    SelectObject(dc, mintPen);
    Arc(dc, left + size / 12, top + size / 7, right - size / 12, bottom - size / 8, right, top + size / 2, left, bottom - size / 3);

    SelectObject(dc, oldPen);
    DeleteObject(tealPen);
    DeleteObject(mintPen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));

    wchar_t mark[] = L"a";
    HFONT font = CreateFontW(
        -std::max(12, size * 2 / 3),
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, mark, 1, &circle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void drawBackForwardIcon(HDC dc, RECT rect, bool forward, bool enabled)
{
    HPEN pen = CreatePen(PS_SOLID, 2, enabled ? RGB(35, 40, 43) : RGB(175, 180, 182));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    POINT points[3] { };

    int midY = (rect.top + rect.bottom) / 2;
    int x = forward ? rect.right - 9 : rect.left + 9;
    if (forward) {
        points[0] = { x - 7, midY - 8 };
        points[1] = { x + 2, midY };
        points[2] = { x - 7, midY + 8 };
    } else {
        points[0] = { x + 7, midY - 8 };
        points[1] = { x - 2, midY };
        points[2] = { x + 7, midY + 8 };
    }

    MoveToEx(dc, points[0].x, points[0].y, nullptr);
    LineTo(dc, points[1].x, points[1].y);
    LineTo(dc, points[2].x, points[2].y);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawReloadIcon(HDC dc, RECT rect, bool loading)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(35, 40, 43));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    if (loading) {
        MoveToEx(dc, rect.left + 10, rect.top + 10, nullptr);
        LineTo(dc, rect.right - 10, rect.bottom - 10);
        MoveToEx(dc, rect.right - 10, rect.top + 10, nullptr);
        LineTo(dc, rect.left + 10, rect.bottom - 10);
    } else {
        Arc(dc, rect.left + 9, rect.top + 9, rect.right - 9, rect.bottom - 9, rect.right - 7, rect.top + 12, rect.right - 2, rect.bottom / 2);
        MoveToEx(dc, rect.right - 7, rect.top + 12, nullptr);
        LineTo(dc, rect.right - 1, rect.top + 12);
        LineTo(dc, rect.right - 1, rect.top + 18);
    }

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawSidebarIcon(HDC dc, RECT rect)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(35, 40, 43));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    Rectangle(dc, rect.left + 8, rect.top + 8, rect.right - 8, rect.bottom - 8);
    MoveToEx(dc, (rect.left + rect.right) / 2, rect.top + 8, nullptr);
    LineTo(dc, (rect.left + rect.right) / 2, rect.bottom - 8);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawShareIcon(HDC dc, RECT rect)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(35, 40, 43));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int x = (rect.left + rect.right) / 2;
    MoveToEx(dc, x, rect.top + 10, nullptr);
    LineTo(dc, x, rect.bottom - 10);
    MoveToEx(dc, x, rect.top + 10, nullptr);
    LineTo(dc, x - 6, rect.top + 16);
    MoveToEx(dc, x, rect.top + 10, nullptr);
    LineTo(dc, x + 6, rect.top + 16);
    MoveToEx(dc, rect.left + 10, rect.bottom - 12, nullptr);
    LineTo(dc, rect.right - 10, rect.bottom - 12);
    Rectangle(dc, rect.left + 9, rect.top + 12, rect.right - 9, rect.bottom - 11);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawDownloadIcon(HDC dc, RECT rect)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(35, 40, 43));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int x = (rect.left + rect.right) / 2;
    MoveToEx(dc, x, rect.top + 8, nullptr);
    LineTo(dc, x, rect.bottom - 13);
    MoveToEx(dc, x, rect.bottom - 13, nullptr);
    LineTo(dc, x - 7, rect.bottom - 20);
    MoveToEx(dc, x, rect.bottom - 13, nullptr);
    LineTo(dc, x + 7, rect.bottom - 20);
    MoveToEx(dc, rect.left + 9, rect.bottom - 8, nullptr);
    LineTo(dc, rect.right - 9, rect.bottom - 8);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawMenuIcon(HDC dc, RECT rect)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(35, 40, 43));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int x = (rect.left + rect.right) / 2;
    for (int y : { rect.top + 11, rect.top + 17, rect.top + 23 })
        LineTo(dc, x + 7, y);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK addressBarProcedure(HWND, UINT, WPARAM, LPARAM);

struct TabState {
    BrowserState* browser { nullptr };
    WKRetainPtr<WKViewRef> view;
    std::wstring title { L"New Tab" };
};

struct BrowserState {
    HWND window { nullptr };
    HWND addressBar { nullptr };
    WNDPROC addressBarOriginalProcedure { nullptr };

    HFONT uiFont { nullptr };
    HBRUSH windowBrush { nullptr };
    HMENU menu { nullptr };

    WKRetainPtr<WKWebsiteDataStoreConfigurationRef> websiteDataStoreConfiguration;
    WKRetainPtr<WKWebsiteDataStoreRef> websiteDataStore;
    WKRetainPtr<WKContextConfigurationRef> contextConfiguration;
    WKRetainPtr<WKContextRef> context;
    WKRetainPtr<WKPreferencesRef> preferences;
    WKRetainPtr<WKPageConfigurationRef> pageConfiguration;

    std::vector<std::unique_ptr<TabState>> tabs;
    size_t activeTab { 0 };

    int tabHeight() const
    {
        return scaleForDpi(window, kTabBarHeight);
    }

    int toolbarHeight() const
    {
        return scaleForDpi(window, kToolbarHeight);
    }

    bool initialize(HWND parentWindow)
    {
        window = parentWindow;

        websiteDataStoreConfiguration = adoptWK(WKWebsiteDataStoreConfigurationCreate());
        if (!websiteDataStoreConfiguration)
            return false;

        websiteDataStore = adoptWK(WKWebsiteDataStoreCreateWithConfiguration(websiteDataStoreConfiguration.get()));
        if (!websiteDataStore)
            return false;

        contextConfiguration = adoptWK(WKContextConfigurationCreate());
        if (!contextConfiguration)
            return false;

        context = adoptWK(WKContextCreateWithConfiguration(contextConfiguration.get()));
        if (!context)
            return false;

        preferences = adoptWK(WKPreferencesCreate());
        if (!preferences)
            return false;

        pageConfiguration = adoptWK(WKPageConfigurationCreate());
        if (!pageConfiguration)
            return false;

        WKPageConfigurationSetWebsiteDataStore(pageConfiguration.get(), websiteDataStore.get());
        WKPageConfigurationSetContext(pageConfiguration.get(), context.get());
        WKPageConfigurationSetPreferences(pageConfiguration.get(), preferences.get());

        uiFont = CreateFontW(
            -12,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );

        windowBrush = CreateSolidBrush(RGB(255, 255, 255));
        createAddressBar();
        createMenu();

        if (!addTab(true))
            return false;

        resize();
        return true;
    }

    ~BrowserState()
    {
        if (addressBar && addressBarOriginalProcedure)
            SetWindowLongPtrW(addressBar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(addressBarOriginalProcedure));

        if (uiFont)
            DeleteObject(uiFont);
        if (windowBrush)
            DeleteObject(windowBrush);
        if (menu)
            DestroyMenu(menu);
    }

    TabState* active()
    {
        if (tabs.empty())
            return nullptr;
        if (activeTab >= tabs.size())
            activeTab = tabs.size() - 1;
        return tabs[activeTab].get();
    }

    void createAddressBar()
    {
        addressBar = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT | ES_NOHIDESEL,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandAddress)),
            GetModuleHandleW(nullptr),
            nullptr
        );

        SendMessageW(addressBar, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        addressBarOriginalProcedure = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(addressBar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(addressBarProcedure))
        );
        SetWindowLongPtrW(addressBar, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }

    void createMenu()
    {
        menu = CreatePopupMenu();
        if (!menu)
            return;

        AppendMenuW(menu, MF_STRING, kMenuNewTab, L"New Tab");
        AppendMenuW(menu, MF_STRING, kMenuNewWindow, L"New Window");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuBack, L"Back");
        AppendMenuW(menu, MF_STRING, kMenuForward, L"Forward");
        AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload");

        HMENU zoom = CreatePopupMenu();
        AppendMenuW(zoom, MF_STRING, kMenuZoomIn, L"Zoom In");
        AppendMenuW(zoom, MF_STRING, kMenuZoomOut, L"Zoom Out");
        AppendMenuW(zoom, MF_STRING, kMenuResetZoom, L"Reset Zoom");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(zoom), L"Page Zoom");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuAbout, L"About Aurora");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit Aurora");
    }

    void loadStartPage(TabState& tab)
    {
        static constexpr char html[] = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aurora</title>
<style>
:root { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; color-scheme: light; }
* { box-sizing: border-box; }
html, body { margin:0; width:100%; min-height:100%; }
body {
    min-height:100vh;
    display:flex;
    align-items:center;
    justify-content:center;
    overflow:auto;
    background:
      radial-gradient(ellipse at 25% 18%, rgba(99,220,202,.48), transparent 38%),
      radial-gradient(ellipse at 73% 22%, rgba(136,112,255,.38), transparent 40%),
      radial-gradient(ellipse at 54% 80%, rgba(55,205,255,.22), transparent 44%),
      linear-gradient(155deg,#091622 0%,#142746 45%,#25193f 72%,#07101d 100%);
    color:#f5fbff;
}
.scene { position:fixed; inset:0; overflow:hidden; }
.glow { position:absolute; border-radius:50%; filter:blur(44px); opacity:.56; }
.g1 { width:45vw; height:22vw; left:-10vw; top:4vh; background:rgba(67,225,183,.32); transform:rotate(-12deg); }
.g2 { width:38vw; height:24vw; right:-8vw; top:10vh; background:rgba(136,104,255,.32); transform:rotate(18deg); }
.g3 { width:50vw; height:20vw; left:25vw; bottom:-6vh; background:rgba(34,203,255,.20); transform:rotate(-4deg); }
.content {
    position:relative;
    z-index:2;
    width:min(930px, 90vw);
    padding:52px 24px 42px;
    text-align:center;
}
.logo {
    width:88px;
    height:88px;
    margin:0 auto 22px;
    filter:drop-shadow(0 18px 32px rgba(0,0,0,.22));
}
.logo svg { width:100%; height:100%; display:block; }
.brand {
    font-size:38px;
    font-weight:500;
    letter-spacing:.21em;
    margin:0;
}
.tag {
    margin-top:9px;
    color:rgba(236,247,252,.72);
    letter-spacing:.34em;
    font-size:12px;
}
.search {
    margin:35px auto 24px;
    width:min(720px, 92vw);
    height:54px;
    padding:0 22px;
    border:1px solid rgba(255,255,255,.35);
    border-radius:28px;
    background:rgba(255,255,255,.14);
    color:#fff;
    outline:none;
    backdrop-filter:blur(18px);
    box-shadow:0 16px 48px rgba(0,0,0,.18);
    font-size:16px;
}
.search::placeholder { color:rgba(247,252,255,.66); }
.tiles { display:flex; flex-wrap:wrap; justify-content:center; gap:14px; margin-top:14px; }
.tile {
    width:145px;
    height:116px;
    border:1px solid rgba(255,255,255,.22);
    border-radius:18px;
    background:rgba(255,255,255,.10);
    display:flex;
    flex-direction:column;
    align-items:center;
    justify-content:center;
    gap:13px;
    color:#eef6fb;
    box-shadow:0 18px 40px rgba(0,0,0,.14);
    backdrop-filter:blur(16px);
}
.tile .icon { width:34px; height:34px; border-radius:11px; display:grid; place-items:center; background:rgba(255,255,255,.16); font-size:18px; font-weight:600; }
.tile span:last-child { font-size:14px; }
.footer { margin-top:22px; color:rgba(240,249,252,.52); font-size:12px; }
</style>
</head>
<body>
<div class="scene"><div class="glow g1"></div><div class="glow g2"></div><div class="glow g3"></div></div>
<div class="content">
<div class="logo">
<svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg" aria-label="Aurora">
<defs>
  <linearGradient id="g1" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#0d6f86"/><stop offset=".5" stop-color="#27cdb0"/><stop offset="1" stop-color="#8ef58b"/>
  </linearGradient>
  <linearGradient id="g2" x1="1" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#28b6d3"/><stop offset=".52" stop-color="#31d6af"/><stop offset="1" stop-color="#0b5475"/>
  </linearGradient>
</defs>
<circle cx="50" cy="50" r="48" fill="url(#g1)"/>
<path d="M12 48 C23 20, 60 8, 88 25 C66 24, 45 38, 32 61 C23 77, 15 73, 12 48Z" fill="url(#g2)" opacity=".84"/>
<path d="M14 65 C31 43, 52 32, 82 39 C65 44, 52 56, 43 72 C35 86, 20 83, 14 65Z" fill="#8ceab0" opacity=".66"/>
<path d="M19 26 C31 34, 42 39, 59 40 C72 41, 83 48, 88 59 C79 47, 62 45, 49 48 C34 51, 25 43, 19 26Z" fill="#13a9c5" opacity=".55"/>
<text x="50" y="66" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="60" font-weight="700" fill="white">a</text>
</svg>
</div>
<h1 class="brand">AURORA</h1>
<div class="tag">BROWSE BEYOND</div>
<input class="search" placeholder="Search the web or enter an address">
<div class="tiles">
  <div class="tile"><div class="icon">▶</div><span>YouTube</span></div>
  <div class="tile"><div class="icon">R</div><span>Reddit</span></div>
  <div class="tile"><div class="icon">G</div><span>GitHub</span></div>
  <div class="tile"><div class="icon">W</div><span>Wikipedia</span></div>
  <div class="tile"><div class="icon">X</div><span>X</span></div>
  <div class="tile"><div class="icon">+</div><span>Add Shortcut</span></div>
</div>
<div class="footer">Aurora is built independently with WebKit for Windows.</div>
</div>
</body>
</html>
)HTML";

        auto page = WKViewGetPage(tab.view.get());
        auto htmlString = adoptWK(WKStringCreateWithUTF8CString(html));
        auto baseURL = createWKURL(L"about:blank");
        WKPageLoadHTMLString(page, htmlString.get(), baseURL.get());

        tab.title = L"New Tab";
        SetWindowTextW(addressBar, L"");
        if (&tab == active())
            SetWindowTextW(window, windowTitle);
    }

    void loadURL(TabState& tab, const std::wstring& value)
    {
        auto page = WKViewGetPage(tab.view.get());
        if (!page)
            return;

        auto url = createWKURL(value);
        if (!url)
            return;

        WKPageLoadURL(page, url.get());
    }

    void navigateInput(const std::wstring& input)
    {
        std::wstring cleaned = input;
        while (!cleaned.empty() && (cleaned.front() == L' ' || cleaned.front() == L'\t'))
            cleaned.erase(cleaned.begin());
        while (!cleaned.empty() && (cleaned.back() == L' ' || cleaned.back() == L'\t'))
            cleaned.pop_back();

        if (cleaned.empty())
            return;

        auto* tab = active();
        if (!tab)
            return;

        if (cleaned.find(L"://") != std::wstring::npos
            || cleaned.rfind(L"about:", 0) == 0
            || cleaned.rfind(L"file:", 0) == 0
            || cleaned.rfind(L"localhost", 0) == 0) {
            loadURL(*tab, cleaned);
        } else {
            auto encoded = percentEncode(cleaned);
            auto searchURL = std::wstring(L"https://www.google.com/search?q=") + encoded;
            loadURL(*tab, searchURL);
        }

        SetFocus(WKViewGetWindow(tab->view.get()));
    }

    bool addTab(bool select)
    {
        auto tab = std::make_unique<TabState>();
        tab->browser = this;

        RECT initialRect { };
        tab->view = adoptWK(WKViewCreate(initialRect, pageConfiguration.get(), window));
        if (!tab->view)
            return false;

        WKViewSetIsInWindow(tab->view.get(), true);

        WKPageRef page = WKViewGetPage(tab->view.get());
        if (!page)
            return false;

        WKPageStateClientV0 stateClient { };
        stateClient.base.version = 0;
        stateClient.base.clientInfo = tab.get();
        stateClient.didChangeIsLoading = didChangeIsLoading;
        stateClient.didChangeTitle = didChangeTitle;
        stateClient.didChangeActiveURL = didChangeActiveURL;
        stateClient.didChangeEstimatedProgress = didChangeEstimatedProgress;
        stateClient.didChangeCanGoBack = didChangeCanGoBack;
        stateClient.didChangeCanGoForward = didChangeCanGoForward;
        WKPageSetPageStateClient(page, &stateClient.base);

        WKPageSetCustomBackingScaleFactor(page, static_cast<double>(windowDpi(window)) / 96.0);

        tabs.push_back(std::move(tab));
        size_t index = tabs.size() - 1;

        if (select)
            activeTab = index;

        loadStartPage(*tabs[index]);
        resize();
        return true;
    }

    void activateTab(size_t index)
    {
        if (index >= tabs.size())
            return;

        activeTab = index;

        for (size_t i = 0; i < tabs.size(); ++i) {
            if (!tabs[i]->view)
                continue;
            HWND viewWindow = WKViewGetWindow(tabs[i]->view.get());
            if (viewWindow)
                ShowWindow(viewWindow, i == activeTab ? SW_SHOW : SW_HIDE);
        }

        if (auto* tab = active()) {
            auto url = createString(adoptWK(WKPageCopyActiveURL(WKViewGetPage(tab->view.get()))).get());
            if (url != L"about:blank")
                SetWindowTextW(addressBar, url.c_str());
            else
                SetWindowTextW(addressBar, L"");

            SetFocus(WKViewGetWindow(tab->view.get()));
        }

        resize();
        InvalidateRect(window, nullptr, TRUE);
    }

    void closeTab(size_t index)
    {
        if (index >= tabs.size())
            return;

        if (tabs.size() == 1) {
            PostMessageW(window, WM_CLOSE, 0, 0);
            return;
        }

        tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(index));
        if (activeTab >= tabs.size())
            activeTab = tabs.size() - 1;
        else if (activeTab > index)
            --activeTab;

        activateTab(activeTab);
    }

    void closeActiveTab()
    {
        closeTab(activeTab);
    }

    void back()
    {
        if (auto* tab = active()) {
            auto page = WKViewGetPage(tab->view.get());
            if (WKPageCanGoBack(page))
                WKPageGoBack(page);
        }
    }

    void forward()
    {
        if (auto* tab = active()) {
            auto page = WKViewGetPage(tab->view.get());
            if (WKPageCanGoForward(page))
                WKPageGoForward(page);
        }
    }

    void reloadOrStop()
    {
        if (auto* tab = active()) {
            auto page = WKViewGetPage(tab->view.get());
            if (WKPageGetEstimatedProgress(page) < 1.0)
                WKPageStopLoading(page);
            else
                WKPageReload(page);
        }
    }

    void updateTabTitle(TabState& tab)
    {
        auto page = WKViewGetPage(tab.view.get());
        auto title = createString(adoptWK(WKPageCopyTitle(page)).get());
        if (!title.empty())
            tab.title = title;
        else
            tab.title = L"New Tab";

        InvalidateRect(window, nullptr, TRUE);

        if (&tab == active()) {
            std::wstring fullTitle = tab.title + L" — Aurora";
            SetWindowTextW(window, fullTitle.c_str());
        }
    }

    void updateAddress(TabState& tab)
    {
        if (&tab != active())
            return;

        auto page = WKViewGetPage(tab.view.get());
        auto url = createString(adoptWK(WKPageCopyActiveURL(page)).get());

        if (url == L"about:blank")
            SetWindowTextW(addressBar, L"");
        else
            SetWindowTextW(addressBar, url.c_str());
    }

    void resize() const
    {
        if (!window)
            return;

        RECT client { };
        if (!GetClientRect(window, &client))
            return;

        int width = client.right - client.left;
        int height = client.bottom - client.top;
        int tabsTop = tabHeight();
        int toolbarTop = tabsTop;
        int contentTop = tabsTop + toolbarHeight();

        int navX = scaleForDpi(window, kToolbarHorizontalPadding);
        int button = scaleForDpi(window, kToolbarButtonSize);
        int gap = scaleForDpi(window, kToolbarGap);

        int rightButtonCount = 3;
        int rightWidth = button * rightButtonCount + gap * 2 + scaleForDpi(window, 20);

        int addressLeft = navX + button * 4 + gap * 3 + scaleForDpi(window, 18);
        int addressRight = width - navX - rightWidth;

        MoveWindow(
            addressBar,
            addressLeft + scaleForDpi(window, 28),
            toolbarTop + scaleForDpi(window, 8),
            std::max(scaleForDpi(window, 180), addressRight - addressLeft - scaleForDpi(window, 56)),
            scaleForDpi(window, 38),
            TRUE
        );

        for (const auto& tab : tabs) {
            if (!tab->view)
                continue;

            HWND viewWindow = WKViewGetWindow(tab->view.get());
            if (!viewWindow)
                continue;

            MoveWindow(
                viewWindow,
                0,
                contentTop,
                width,
                std::max(0, height - contentTop),
                TRUE
            );
        }

        InvalidateRect(window, nullptr, TRUE);
    }

    RECT addressPillRect() const
    {
        RECT client { };
        GetClientRect(window, &client);

        int navX = scaleForDpi(window, kToolbarHorizontalPadding);
        int button = scaleForDpi(window, kToolbarButtonSize);
        int gap = scaleForDpi(window, kToolbarGap);
        int rightButtonCount = 3;
        int rightWidth = button * rightButtonCount + gap * 2 + scaleForDpi(window, 20);

        int left = navX + button * 4 + gap * 3 + scaleForDpi(window, 18);
        int right = client.right - navX - rightWidth;

        return {
            left,
            tabHeight() + scaleForDpi(window, 6),
            right,
            tabHeight() + toolbarHeight() - scaleForDpi(window, 6)
        };
    }

    void showMenu()
    {
        RECT client { };
        GetClientRect(window, &client);
        int button = scaleForDpi(window, kToolbarButtonSize);
        int gap = scaleForDpi(window, kToolbarGap);
        int navX = scaleForDpi(window, kToolbarHorizontalPadding);

        POINT point {
            client.right - navX - button / 2,
            tabHeight() + toolbarHeight() - gap
        };
        ClientToScreen(window, &point);

        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
    }

    void handleCommand(UINT command)
    {
        switch (command) {
        case kCommandBack:
            back();
            break;
        case kCommandForward:
            forward();
            break;
        case kCommandReload:
            reloadOrStop();
            break;
        case kCommandSidebar:
            MessageBoxW(window, L"Sidebar UI placeholder. Bookmarks and history will be added here next.", L"Aurora", MB_OK);
            break;
        case kCommandNewTab:
            addTab(true);
            break;
        case kCommandMenu:
            showMenu();
            break;
        case kCommandAddress:
            SetFocus(addressBar);
            break;
        case kCommandCloseTab:
            closeActiveTab();
            break;

        case kMenuNewTab:
            addTab(true);
            break;
        case kMenuNewWindow: {
            MessageBoxW(window, L"New Window is planned for the next browser-core pass.", L"Aurora", MB_OK);
            break;
        }
        case kMenuBack:
            back();
            break;
        case kMenuForward:
            forward();
            break;
        case kMenuReload:
            reloadOrStop();
            break;
        case kMenuZoomIn:
            if (auto* tab = active()) {
                auto page = WKViewGetPage(tab->view.get());
                WKPageSetPageZoomFactor(page, WKPageGetPageZoomFactor(page) * 1.1);
            }
            break;
        case kMenuZoomOut:
            if (auto* tab = active()) {
                auto page = WKViewGetPage(tab->view.get());
                WKPageSetPageZoomFactor(page, WKPageGetPageZoomFactor(page) / 1.1);
            }
            break;
        case kMenuResetZoom:
            if (auto* tab = active())
                WKPageSetPageZoomFactor(WKViewGetPage(tab->view.get()), 1.0);
            break;
        case kMenuAbout:
            MessageBoxW(
                window,
                L"Aurora\n\nA WebKit browser for Windows.\n\nAurora branding is original and replaceable.",
                L"About Aurora",
                MB_OK | MB_ICONINFORMATION
            );
            break;
        case kMenuQuit:
            PostMessageW(window, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }

        InvalidateRect(window, nullptr, TRUE);
    }

    bool hitTest(POINT point)
    {
        int dpi = static_cast<int>(windowDpi(window));
        int tabH = MulDiv(kTabBarHeight, dpi, 96);
        int toolbarH = MulDiv(kToolbarHeight, dpi, 96);
        int button = MulDiv(kToolbarButtonSize, dpi, 96);
        int gap = MulDiv(kToolbarGap, dpi, 96);
        int leftPad = MulDiv(kToolbarHorizontalPadding, dpi, 96);

        if (point.y < tabH) {
            int plusLeft = GetClientWidth(window) - leftPad - button;
            if (point.x >= plusLeft && point.x <= plusLeft + button)
                return handleHit(kCommandNewTab);

            int available = std::max(1, plusLeft - leftPad - MulDiv(54, dpi, 96));
            int tabWidth = std::clamp(available / std::max(1, static_cast<int>(tabs.size())), MulDiv(kTabMinWidth, dpi, 96), MulDiv(kTabMaxWidth, dpi, 96));

            for (size_t i = 0; i < tabs.size(); ++i) {
                int left = leftPad + MulDiv(46, dpi, 96) + static_cast<int>(i) * (tabWidth + gap);
                if (point.x >= left && point.x < left + tabWidth)
                    return handleTabHit(point, i, left, tabWidth);
            }
            return true;
        }

        if (point.y < tabH + toolbarH) {
            int x = leftPad;
            if (hitRect(point, x, tabH, button, toolbarH, gap))
                return handleHit(kCommandBack);
            x += button + gap;
            if (hitRect(point, x, tabH, button, toolbarH, gap))
                return handleHit(kCommandForward);
            x += button + gap;
            if (hitRect(point, x, tabH, button, toolbarH, gap))
                return handleHit(kCommandSidebar);
            x += button + gap;
            if (hitRect(point, x, tabH, button, toolbarH, gap))
                return handleHit(kCommandReload);

            RECT pill = addressPillRect();
            int menuX = pill.right + gap;
            if (point.x >= menuX + gap * 2 && point.x < menuX + button * 3 + gap * 2) {
                int relative = point.x - (menuX + gap * 2);
                int index = relative / (button + gap);
                if (index == 0)
                    return handleHit(kCommandMenu);
                if (index == 1)
                    return handleHit(kCommandMenu);
                return handleHit(kCommandMenu);
            }
            return true;
        }

        return false;
    }

    static int GetClientWidth(HWND hwnd)
    {
        RECT rect { };
        GetClientRect(hwnd, &rect);
        return rect.right;
    }

    static bool hitRect(POINT point, int x, int y, int width, int toolbarHeight, int gap)
    {
        return point.x >= x && point.x <= x + width && point.y >= y + gap && point.y <= y + toolbarHeight - gap;
    }

    bool handleTabHit(POINT point, size_t index, int left, int tabWidth)
    {
        int closeZone = scaleForDpi(window, 28);
        if (point.x > left + tabWidth - closeZone) {
            closeTab(index);
            return true;
        }

        activateTab(index);
        return true;
    }

    bool handleHit(UINT command)
    {
        handleCommand(command);
        return true;
    }

    void paint(HDC dc)
    {
        RECT client { };
        GetClientRect(window, &client);

        int dpi = static_cast<int>(windowDpi(window));
        int tabH = MulDiv(kTabBarHeight, dpi, 96);
        int toolbarH = MulDiv(kToolbarHeight, dpi, 96);
        int button = MulDiv(kToolbarButtonSize, dpi, 96);
        int gap = MulDiv(kToolbarGap, dpi, 96);
        int leftPad = MulDiv(kToolbarHorizontalPadding, dpi, 96);

        HBRUSH topBrush = CreateSolidBrush(RGB(242, 244, 245));
        FillRect(dc, &client, topBrush);
        DeleteObject(topBrush);

        RECT toolbar { 0, tabH, client.right, tabH + toolbarH };
        HBRUSH toolbarBrush = CreateSolidBrush(RGB(232, 235, 237));
        FillRect(dc, &toolbar, toolbarBrush);
        DeleteObject(toolbarBrush);

        // Main Aurora emblem: our temporary replacement for Safari's app identity area.
        RECT logoRect {
            leftPad,
            (tabH - MulDiv(kLogoSize, dpi, 96)) / 2,
            leftPad + MulDiv(kLogoSize, dpi, 96),
            (tabH + MulDiv(kLogoSize, dpi, 96)) / 2
        };
        drawAuroraMark(dc, logoRect, false);

        int plusLeft = client.right - leftPad - button;
        RECT plusRect { plusLeft, 0, plusLeft + button, tabH };
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(35, 40, 43));
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, uiFont));
        DrawTextW(dc, L"+", 1, &plusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldFont);

        int available = std::max(1, plusLeft - leftPad - MulDiv(54, dpi, 96));
        int tabWidth = std::clamp(available / std::max(1, static_cast<int>(tabs.size())), MulDiv(kTabMinWidth, dpi, 96), MulDiv(kTabMaxWidth, dpi, 96));

        for (size_t i = 0; i < tabs.size(); ++i) {
            int left = leftPad + MulDiv(46, dpi, 96) + static_cast<int>(i) * (tabWidth + gap);
            RECT tabRect {
                left,
                MulDiv(5, dpi, 96),
                left + tabWidth,
                tabH - MulDiv(4, dpi, 96)
            };

            HBRUSH brush = CreateSolidBrush(i == activeTab ? RGB(255, 255, 255) : RGB(229, 232, 234));
            HPEN pen = CreatePen(PS_SOLID, 1, i == activeTab ? RGB(210, 215, 217) : RGB(225, 228, 229));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
            HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
            RoundRect(dc, tabRect.left, tabRect.top, tabRect.right, tabRect.bottom, MulDiv(11, dpi, 96), MulDiv(11, dpi, 96));
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);

            RECT tabIcon = tabRect;
            int iconSize = MulDiv(20, dpi, 96);
            tabIcon.left += MulDiv(10, dpi, 96);
            tabIcon.top = tabRect.top + (tabRect.bottom - tabRect.top - iconSize) / 2;
            tabIcon.right = tabIcon.left + iconSize;
            tabIcon.bottom = tabIcon.top + iconSize;
            drawAuroraMark(dc, tabIcon, false);

            RECT titleRect = tabRect;
            titleRect.left = tabIcon.right + MulDiv(8, dpi, 96);
            titleRect.right -= MulDiv(30, dpi, 96);
            SetTextColor(dc, RGB(45, 49, 52));
            SelectObject(dc, uiFont);
            DrawTextW(dc, tabs[i]->title.c_str(), -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            RECT closeRect = tabRect;
            closeRect.left = tabRect.right - MulDiv(30, dpi, 96);
            SetTextColor(dc, RGB(95, 100, 103));
            DrawTextW(dc, L"×", 1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Toolbar icon cells.
        int x = leftPad;
        RECT backRect { x, tabH, x + button, tabH + toolbarH };
        drawBackForwardIcon(dc, backRect, false, active() && WKPageCanGoBack(WKViewGetPage(active()->view.get())));
        x += button + gap;
        RECT forwardRect { x, tabH, x + button, tabH + toolbarH };
        drawBackForwardIcon(dc, forwardRect, true, active() && WKPageCanGoForward(WKViewGetPage(active()->view.get())));
        x += button + gap;
        RECT sidebarRect { x, tabH, x + button, tabH + toolbarH };
        drawSidebarIcon(dc, sidebarRect);
        x += button + gap;
        RECT reloadRect { x, tabH, x + button, tabH + toolbarH };
        bool loading = active() && WKPageGetEstimatedProgress(WKViewGetPage(active()->view.get())) < 1.0;
        drawReloadIcon(dc, reloadRect, loading);

        RECT pill = addressPillRect();
        HBRUSH pillBrush = CreateSolidBrush(RGB(255, 255, 255));
        HPEN pillPen = CreatePen(PS_SOLID, 1, RGB(204, 210, 213));
        HBRUSH oldBrush2 = static_cast<HBRUSH>(SelectObject(dc, pillBrush));
        HPEN oldPen2 = static_cast<HPEN>(SelectObject(dc, pillPen));
        RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom, MulDiv(20, dpi, 96), MulDiv(20, dpi, 96));
        SelectObject(dc, oldBrush2);
        SelectObject(dc, oldPen2);
        DeleteObject(pillBrush);
        DeleteObject(pillPen);

        RECT shareRect { pill.right + gap * 2, tabH, pill.right + gap * 2 + button, tabH + toolbarH };
        RECT downloadRect { shareRect.right + gap, tabH, shareRect.right + gap + button, tabH + toolbarH };
        RECT menuRect { downloadRect.right + gap, tabH, downloadRect.right + gap + button, tabH + toolbarH };
        drawShareIcon(dc, shareRect);
        drawDownloadIcon(dc, downloadRect);
        drawMenuIcon(dc, menuRect);
    }
};

LRESULT CALLBACK addressBarProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* browser = reinterpret_cast<BrowserState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (browser && message == WM_KEYDOWN && wParam == VK_RETURN) {
        wchar_t text[4096] { };
        GetWindowTextW(window, text, static_cast<int>(std::size(text)));
        browser->navigateInput(text);
        return 0;
    }

    if (browser && message == WM_SETFOCUS) {
        LRESULT result = CallWindowProcW(browser->addressBarOriginalProcedure, window, message, wParam, lParam);
        SendMessageW(window, EM_SETSEL, 0, -1);
        return result;
    }

    if (browser && browser->addressBarOriginalProcedure)
        return CallWindowProcW(browser->addressBarOriginalProcedure, window, message, wParam, lParam);

    return DefWindowProcW(window, message, wParam, lParam);
}

void didChangeIsLoading(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        InvalidateRect(tab->browser->window, nullptr, TRUE);
}

void didChangeTitle(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        tab->browser->updateTabTitle(*tab);
}

void didChangeActiveURL(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        tab->browser->updateAddress(*tab);
}

void didChangeEstimatedProgress(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        InvalidateRect(tab->browser->window, nullptr, TRUE);
}

void didChangeCanGoBack(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        InvalidateRect(tab->browser->window, nullptr, TRUE);
}

void didChangeCanGoForward(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        InvalidateRect(tab->browser->window, nullptr, TRUE);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        auto* createInfo = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* state = static_cast<BrowserState*>(createInfo->lpCreateParams);
        if (!state)
            return FALSE;

        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    auto* state = reinterpret_cast<BrowserState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE:
        if (!state || !state->initialize(window)) {
            MessageBoxW(window, L"WebKit could not initialize Aurora.", windowTitle, MB_OK | MB_ICONERROR);
            return -1;
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint { };
        HDC dc = BeginPaint(window, &paint);
        if (state)
            state->paint(dc);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        if (state) {
            POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (state->hitTest(point))
                return 0;
        }
        break;
    }

    case WM_COMMAND:
        if (state)
            state->handleCommand(LOWORD(wParam));
        return 0;

    case WM_SIZE:
        if (state)
            state->resize();
        return 0;

    case WM_DPICHANGED:
        if (state) {
            for (auto& tab : state->tabs) {
                if (!tab->view)
                    continue;
                auto page = WKViewGetPage(tab->view.get());
                if (page)
                    WKPageSetCustomBackingScaleFactor(page, static_cast<double>(windowDpi(window)) / 96.0);
            }
            state->resize();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    enableDpiAwareness();

    WNDCLASSEXW windowClass { };
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = windowClassName;

    if (!RegisterClassExW(&windowClass)) {
        MessageBoxW(nullptr, L"Could not register the Aurora window class.", windowTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    BrowserState browserState;

    HWND window = CreateWindowExW(
        0,
        windowClassName,
        windowTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        840,
        nullptr,
        nullptr,
        instance,
        &browserState
    );

    if (!window) {
        MessageBoxW(nullptr, L"Could not create the Aurora window.", windowTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    ACCEL accelerators[] = {
        { FCONTROL, 'L', kCommandAddress },
        { FCONTROL, 'R', kCommandReload },
        { FCONTROL, 'T', kCommandNewTab },
        { FCONTROL, 'W', kCommandCloseTab },
        { FALT, VK_LEFT, kCommandBack },
        { FALT, VK_RIGHT, kCommandForward }
    };

    HACCEL acceleratorTable = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message { };
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window, acceleratorTable, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (acceleratorTable)
        DestroyAcceleratorTable(acceleratorTable);

    return static_cast<int>(message.wParam);
}
