#include <windows.h>
#include <windowsx.h>

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

#include <WebKit/WKContext.h>
#include <WebKit/WKDownloadClient.h>
#include <WebKit/WKDownloadRef.h>
#include <WebKit/WKNavigationActionRef.h>
#include <WebKit/WKNavigationResponseRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKURLResponse.h>
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
#include <chrono>
#include <cwchar>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <ctime>
#include <thread>
#include <utility>
#include <vector>

#include <shlobj.h>
#include <shellapi.h>
#include <urlmon.h>

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER (WM_USER + 1)
#endif

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
constexpr UINT kCommandShare = 1009;
constexpr UINT kCommandDownloads = 1010;
constexpr UINT kCommandMinimize = 1011;
constexpr UINT kCommandMaximize = 1012;
constexpr UINT kCommandCloseWindow = 1013;
constexpr UINT kCommandPrivacy = 1014;
constexpr UINT kCommandReader = 1015;
constexpr UINT WM_AURORA_FAVICON_READY = WM_APP + 20;

constexpr UINT kMenuNewTab = 2001;
constexpr UINT kMenuNewWindow = 2002;
constexpr UINT kMenuNewPrivateWindow = 2003;
constexpr UINT kMenuHistory = 2004;
constexpr UINT kMenuBookmarks = 2005;
constexpr UINT kMenuDownloads = 2006;
constexpr UINT kMenuSavePage = 2007;
constexpr UINT kMenuPrint = 2008;
constexpr UINT kMenuFindInPage = 2009;
constexpr UINT kMenuZoomIn = 2010;
constexpr UINT kMenuZoomOut = 2011;
constexpr UINT kMenuResetZoom = 2012;
constexpr UINT kMenuStartPage = 2013;
constexpr UINT kMenuSettings = 2014;
constexpr UINT kMenuAbout = 2015;
constexpr UINT kMenuQuit = 2016;
constexpr UINT kMenuAddBookmark = 2017;
constexpr UINT kMenuReadingList = 2018;
constexpr UINT kMenuClearHistory = 2019;
constexpr UINT kMenuExtensions = 2020;

constexpr int kTitleBarHeight = 23;
constexpr int kToolbarHeight = 33;
constexpr int kToolbarHorizontalPadding = 12;
constexpr int kToolbarButtonSize = 24;
constexpr int kToolbarGap = 4;
constexpr int kLogoSize = 17;
constexpr int kTabMinWidth = 126;
constexpr int kTabMaxWidth = 220;
constexpr int kTrafficLightSize = 12;
constexpr int kTrafficLightGap = 8;
constexpr int kResizeBorder = 4;

struct BrowserState;
struct TabState;

void didChangeIsLoading(const void*);
void didChangeTitle(const void*);
void didChangeActiveURL(const void*);
void didChangeEstimatedProgress(const void*);
void didChangeCanGoBack(const void*);
void didChangeCanGoForward(const void*);
void didFailProvisionalNavigation(WKPageRef, WKNavigationRef, WKErrorRef, WKTypeRef, const void*);
void navigationActionDidBecomeDownload(WKPageRef, WKNavigationActionRef, WKDownloadRef, const void*);
void navigationResponseDidBecomeDownload(WKPageRef, WKNavigationResponseRef, WKDownloadRef, const void*);
void contextMenuDidCreateDownload(WKPageRef, WKDownloadRef, const void*);

struct FaviconResult {
    std::wstring url;
    HICON icon { nullptr };
};

std::wstring faviconURLForPage(const std::wstring& pageURL)
{
    size_t scheme = pageURL.find(L"://");
    if (scheme == std::wstring::npos)
        return { };

    size_t hostStart = scheme + 3;
    size_t hostEnd = pageURL.find_first_of(L"/?#", hostStart);
    std::wstring host = pageURL.substr(hostStart, hostEnd == std::wstring::npos ? std::wstring::npos : hostEnd - hostStart);
    if (host.empty())
        return { };

    return L"https://" + host + L"/favicon.ico";
}

struct VisitEntry {
    std::wstring url;
    std::wstring title;
    long long visits { 0 };
    long long lastVisited { 0 };
};

struct SavedPage {
    std::wstring url;
    std::wstring title;
    long long added { 0 };
};

struct DownloadEntry {
    std::wstring filename;
    std::wstring path;
    bool failed { false };
};

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

std::wstring fromUTF8(const std::string& value)
{
    if (value.empty())
        return { };

    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
        return { };

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
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
    encoded.reserve(utf8.size() * 3);

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

    return fromUTF8(encoded);
}

std::wstring htmlEscape(const std::wstring& value)
{
    std::wstring result;
    result.reserve(value.size() + 16);

    for (wchar_t ch : value) {
        switch (ch) {
        case L'&':
            result += L"&amp;";
            break;
        case L'<':
            result += L"&lt;";
            break;
        case L'>':
            result += L"&gt;";
            break;
        case L'"':
            result += L"&quot;";
            break;
        case L'\'':
            result += L"&#39;";
            break;
        default:
            result.push_back(ch);
            break;
        }
    }

    return result;
}

std::wstring applicationDataDirectory()
{
    wchar_t buffer[MAX_PATH] { };
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (!length || length >= std::size(buffer))
        return L".";

    std::wstring directory(buffer, length);
    directory += L"\\Aurora";

    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

std::wstring historyPath()
{
    return applicationDataDirectory() + L"\\history.tsv";
}

std::wstring bookmarksPath()
{
    return applicationDataDirectory() + L"\\bookmarks.tsv";
}

std::wstring readingListPath()
{
    return applicationDataDirectory() + L"\\reading-list.tsv";
}

std::wstring extensionsPath()
{
    return applicationDataDirectory() + L"\\extensions";
}

std::wstring downloadsDirectory()
{
    PWSTR rawPath = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &rawPath)) && rawPath) {
        result = rawPath;
        CoTaskMemFree(rawPath);
    }
    if (result.empty()) {
        wchar_t profile[MAX_PATH] { };
        DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        if (length && length < MAX_PATH)
            result = std::wstring(profile, length) + L"\\Downloads";
    }
    return result;
}

long long currentUnixTime()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void replaceTabsAndNewlines(std::wstring& value)
{
    for (auto& ch : value) {
        if (ch == L'\t' || ch == L'\r' || ch == L'\n')
            ch = L' ';
    }
}

double windowScaleFactor(HWND window)
{
    UINT dpi = 96;

    using GetDpiForWindowFunction = UINT (WINAPI*)(HWND);
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
            GetProcAddress(user32, "GetDpiForWindow")
        );
        if (getDpiForWindow) {
            UINT value = getDpiForWindow(window);
            if (value)
                dpi = value;
        }
        FreeLibrary(user32);
    }

    return static_cast<double>(dpi) / 96.0;
}

int scaleForDpi(HWND window, int logicalPixels)
{
    return MulDiv(logicalPixels, static_cast<int>(windowScaleFactor(window) * 96.0), 96);
}

int windowDpi(HWND window)
{
    return static_cast<int>(windowScaleFactor(window) * 96.0);
}

void drawAuroraMark(HDC dc, const RECT& rect, bool darkBackground)
{
    int size = std::min(rect.right - rect.left, rect.bottom - rect.top);
    RECT circle {
        rect.left,
        rect.top,
        rect.left + size,
        rect.top + size
    };

    HBRUSH background = CreateSolidBrush(darkBackground ? RGB(27, 48, 68) : RGB(53, 184, 168));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, background));
    Ellipse(dc, circle.left, circle.top, circle.right, circle.bottom);
    SelectObject(dc, oldBrush);
    DeleteObject(background);

    HPEN teal = CreatePen(PS_SOLID, std::max(1, size / 10), darkBackground ? RGB(63, 222, 201) : RGB(19, 130, 149));
    HPEN mint = CreatePen(PS_SOLID, std::max(1, size / 12), darkBackground ? RGB(174, 248, 178) : RGB(164, 239, 173));

    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, teal));
    Arc(
        dc,
        circle.left + size / 8,
        circle.top + size / 8,
        circle.right - size / 8,
        circle.bottom - size / 8,
        circle.right - size / 4,
        circle.top + size / 4,
        circle.left + size / 4,
        circle.bottom - size / 5
    );

    SelectObject(dc, mint);
    Arc(
        dc,
        circle.left + size / 7,
        circle.top + size / 5,
        circle.right - size / 7,
        circle.bottom - size / 8,
        circle.right - size / 7,
        circle.top + size / 3,
        circle.left + size / 7,
        circle.bottom - size / 3
    );

    SelectObject(dc, oldPen);
    DeleteObject(teal);
    DeleteObject(mint);

    HFONT font = CreateFontW(
        -std::max(10, size * 2 / 3),
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
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, L"a", 1, &circle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void drawChevron(HDC dc, const RECT& rect, bool forward, bool enabled)
{
    HPEN pen = CreatePen(PS_SOLID, 1, enabled ? RGB(55, 60, 63) : RGB(171, 176, 179));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;

    MoveToEx(dc, cx + (forward ? -4 : 4), cy - 7, nullptr);
    LineTo(dc, cx + (forward ? 4 : -4), cy);
    LineTo(dc, cx + (forward ? -4 : 4), cy + 7);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawReload(HDC dc, const RECT& rect, bool loading)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 60, 63));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;

    if (loading) {
        MoveToEx(dc, cx - 6, cy - 6, nullptr);
        LineTo(dc, cx + 6, cy + 6);
        MoveToEx(dc, cx + 6, cy - 6, nullptr);
        LineTo(dc, cx - 6, cy + 6);
    } else {
        Arc(dc, cx - 9, cy - 9, cx + 9, cy + 9, cx + 8, cy - 7, cx + 9, cy + 7);
        MoveToEx(dc, cx + 8, cy - 7, nullptr);
        LineTo(dc, cx + 1, cy - 7);
        LineTo(dc, cx + 8, cy - 1);
    }

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawSidebar(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 60, 63));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    Rectangle(dc, rect.left + 9, rect.top + 9, rect.right - 9, rect.bottom - 9);
    int splitX = rect.left + (rect.right - rect.left) / 2;
    MoveToEx(dc, splitX, rect.top + 9, nullptr);
    LineTo(dc, splitX, rect.bottom - 9);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawShare(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 60, 63));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int x = (rect.left + rect.right) / 2;
    MoveToEx(dc, x, rect.top + 9, nullptr);
    LineTo(dc, x, rect.bottom - 9);
    MoveToEx(dc, x, rect.top + 9, nullptr);
    LineTo(dc, x - 6, rect.top + 15);
    MoveToEx(dc, x, rect.top + 9, nullptr);
    LineTo(dc, x + 6, rect.top + 15);
    Rectangle(dc, rect.left + 9, rect.top + 13, rect.right - 9, rect.bottom - 8);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawDownload(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 60, 63));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int x = (rect.left + rect.right) / 2;
    MoveToEx(dc, x, rect.top + 8, nullptr);
    LineTo(dc, x, rect.bottom - 13);
    MoveToEx(dc, x, rect.bottom - 13, nullptr);
    LineTo(dc, x - 6, rect.bottom - 19);
    MoveToEx(dc, x, rect.bottom - 13, nullptr);
    LineTo(dc, x + 6, rect.bottom - 19);
    MoveToEx(dc, rect.left + 9, rect.bottom - 8, nullptr);
    LineTo(dc, rect.right - 9, rect.bottom - 8);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawPageMenu(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 60, 63));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int left = rect.left + 8;
    int top = rect.top + 7;
    int right = rect.right - 8;
    int bottom = rect.bottom - 7;

    RoundRect(dc, left, top, right, bottom, 2, 2);
    int lineX = left + 4;
    for (int y : { top + 5, top + 9, top + 13 }) {
        MoveToEx(dc, lineX, y, nullptr);
        LineTo(dc, right - 4, y);
    }

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawMenu(HDC dc, const RECT& rect)
{
    HBRUSH brush = CreateSolidBrush(RGB(55, 60, 63));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));

    int x = (rect.left + rect.right) / 2;
    int y = (rect.top + rect.bottom) / 2;
    Ellipse(dc, x - 2, y - 8, x + 2, y - 4);
    Ellipse(dc, x - 2, y - 2, x + 2, y + 2);
    Ellipse(dc, x - 2, y + 4, x + 2, y + 8);

    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

void drawTrafficLight(HDC dc, int x, int y, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
    Ellipse(dc, x, y, x + kTrafficLightSize, y + kTrafficLightSize);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

void drawPlus(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 74, 77));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;
    MoveToEx(dc, cx - 5, cy, nullptr);
    LineTo(dc, cx + 5, cy);
    MoveToEx(dc, cx, cy - 5, nullptr);
    LineTo(dc, cx, cy + 5);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawClose(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(105, 109, 112));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;
    MoveToEx(dc, cx - 3, cy - 3, nullptr);
    LineTo(dc, cx + 3, cy + 3);
    MoveToEx(dc, cx + 3, cy - 3, nullptr);
    LineTo(dc, cx - 3, cy + 3);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawShield(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(67, 72, 75));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;
    POINT outline[] = {
        { cx, cy - 7 },
        { cx + 6, cy - 4 },
        { cx + 5, cy + 2 },
        { cx, cy + 7 },
        { cx - 5, cy + 2 },
        { cx - 6, cy - 4 },
        { cx, cy - 7 }
    };
    Polyline(dc, outline, static_cast<int>(std::size(outline)));

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawReader(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(67, 72, 75));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;
    RoundRect(dc, cx - 7, cy - 7, cx + 7, cy + 7, 3, 3);
    MoveToEx(dc, cx - 4, cy - 3, nullptr);
    LineTo(dc, cx + 4, cy - 3);
    MoveToEx(dc, cx - 4, cy, nullptr);
    LineTo(dc, cx + 4, cy);
    MoveToEx(dc, cx - 4, cy + 3, nullptr);
    LineTo(dc, cx + 4, cy + 3);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}


LRESULT CALLBACK addressBarProcedure(HWND, UINT, WPARAM, LPARAM);

struct TabState {
    BrowserState* browser { nullptr };
    WKRetainPtr<WKViewRef> view;
    std::wstring title { L"New Tab" };
    std::wstring activeUrl;
    std::wstring lastRecordedUrl;
    std::wstring fallbackURL;
    HICON favicon { nullptr };
    std::wstring faviconRequestURL;
};

struct BrowserState {
    HWND window { nullptr };
    HWND addressBar { nullptr };
    bool customMaximized { false };
    RECT restoredWindowRect { };
    WNDPROC addressBarOriginalProcedure { nullptr };

    HFONT uiFont { nullptr };
    HBRUSH addressBarBrush { nullptr };
    HMENU menu { nullptr };
    HMENU bookmarksMenu { nullptr };
    bool sidebarOpen { false };
    int sidebarSection { 0 };

    WKRetainPtr<WKWebsiteDataStoreConfigurationRef> websiteDataStoreConfiguration;
    WKRetainPtr<WKWebsiteDataStoreRef> websiteDataStore;
    WKRetainPtr<WKContextConfigurationRef> contextConfiguration;
    WKRetainPtr<WKContextRef> context;
    WKRetainPtr<WKPreferencesRef> preferences;
    WKRetainPtr<WKPageConfigurationRef> pageConfiguration;

    std::vector<std::unique_ptr<TabState>> tabs;
    std::vector<VisitEntry> visits;
    std::vector<SavedPage> bookmarks;
    std::vector<SavedPage> readingList;
    std::vector<std::wstring> extensionScripts;
    std::vector<std::unique_ptr<TabState>> retiredTabs;
    std::vector<DownloadEntry> downloads;
    size_t activeTab { 0 };

    int titleBarHeight() const { return scaleForDpi(window, kTitleBarHeight); }
    int toolbarHeight() const { return scaleForDpi(window, kToolbarHeight); }

    TabState* active()
    {
        if (tabs.empty())
            return nullptr;
        if (activeTab >= tabs.size())
            activeTab = tabs.size() - 1;
        return tabs[activeTab].get();
    }

    void loadVisitData()
    {
        visits.clear();

        std::wifstream file(historyPath());
        std::wstring line;
        while (std::getline(file, line)) {
            std::wistringstream stream(line);
            std::wstring countText;
            std::wstring timeText;
            std::wstring url;
            std::wstring title;

            if (!std::getline(stream, countText, L'\t'))
                continue;
            if (!std::getline(stream, timeText, L'\t'))
                continue;
            if (!std::getline(stream, url, L'\t'))
                continue;
            if (!std::getline(stream, title))
                title = url;

            VisitEntry entry;
            wchar_t* countEnd = nullptr;
            wchar_t* timeEnd = nullptr;
            entry.visits = std::wcstoll(countText.c_str(), &countEnd, 10);
            entry.lastVisited = std::wcstoll(timeText.c_str(), &timeEnd, 10);
            if (countEnd == countText.c_str() || timeEnd == timeText.c_str())
                continue;
            entry.url = url;
            entry.title = title;
            if (!entry.url.empty())
                visits.push_back(std::move(entry));
        }
    }

    void saveVisitData()
    {
        auto path = historyPath();
        std::wofstream file(path, std::ios::trunc);
        if (!file)
            return;

        for (const auto& entry : visits) {
            std::wstring title = entry.title;
            replaceTabsAndNewlines(title);
            file << entry.visits << L'\t'
                 << entry.lastVisited << L'\t'
                 << entry.url << L'\t'
                 << title << L'\n';
        }
    }

    void recordVisit(const std::wstring& url, const std::wstring& title)
    {
        if (url.empty())
            return;

        if (!(url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0))
            return;

        auto found = std::find_if(visits.begin(), visits.end(), [&](const VisitEntry& entry) {
            return entry.url == url;
        });

        const long long now = currentUnixTime();

        if (found == visits.end()) {
            VisitEntry entry;
            entry.url = url;
            entry.title = title.empty() ? url : title;
            entry.visits = 1;
            entry.lastVisited = now;
            visits.push_back(std::move(entry));
        } else {
            found->visits++;
            found->lastVisited = now;
            if (!title.empty())
                found->title = title;
        }

        std::sort(visits.begin(), visits.end(), [](const VisitEntry& a, const VisitEntry& b) {
            if (a.visits != b.visits)
                return a.visits > b.visits;
            return a.lastVisited > b.lastVisited;
        });

        if (visits.size() > 64)
            visits.resize(64);

        saveVisitData();
    }

    std::vector<VisitEntry> frequentVisits(size_t maximum = 6) const
    {
        auto sorted = visits;
        std::sort(sorted.begin(), sorted.end(), [](const VisitEntry& a, const VisitEntry& b) {
            if (a.visits != b.visits)
                return a.visits > b.visits;
            return a.lastVisited > b.lastVisited;
        });

        if (sorted.size() > maximum)
            sorted.resize(maximum);

        return sorted;
    }

    void loadSavedPages(const std::wstring& path, std::vector<SavedPage>& pages)
    {
        pages.clear();
        std::wifstream file(path);
        std::wstring line;

        while (std::getline(file, line)) {
            std::wistringstream stream(line);
            std::wstring addedText;
            std::wstring url;
            std::wstring title;

            if (!std::getline(stream, addedText, L'\t'))
                continue;
            if (!std::getline(stream, url, L'\t'))
                continue;
            if (!std::getline(stream, title))
                title = url;

            wchar_t* end = nullptr;
            long long added = std::wcstoll(addedText.c_str(), &end, 10);
            if (end == addedText.c_str() || url.empty())
                continue;

            pages.push_back({ url, title.empty() ? url : title, added });
        }
    }

    void saveSavedPages(const std::wstring& path, const std::vector<SavedPage>& pages)
    {
        std::wofstream file(path, std::ios::trunc);
        if (!file)
            return;

        for (const auto& page : pages) {
            std::wstring url = page.url;
            std::wstring title = page.title;
            replaceTabsAndNewlines(url);
            replaceTabsAndNewlines(title);
            file << page.added << L'\t' << url << L'\t' << title << L'\n';
        }
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
            -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        addressBarBrush = CreateSolidBrush(RGB(255, 255, 255));

        loadVisitData();
        loadSavedPages(bookmarksPath(), bookmarks);
        loadSavedPages(readingListPath(), readingList);
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
        if (addressBarBrush)
            DeleteObject(addressBarBrush);
        if (menu)
            DestroyMenu(menu);
    }

    void createAddressBar()
    {
        addressBar = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
            0, 0, 0, 0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandAddress)),
            GetModuleHandleW(nullptr),
            nullptr
        );

        SendMessageW(addressBar, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        SendMessageW(addressBar, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search or enter website address"));
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

        AppendMenuW(menu, MF_STRING, kMenuNewTab, L"New Tab\tCtrl+T");
        AppendMenuW(menu, MF_STRING, kMenuNewWindow, L"New Window\tCtrl+N");
        AppendMenuW(menu, MF_STRING, kMenuNewPrivateWindow, L"New Private Window\tCtrl+Shift+P");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        HMENU historyMenu = CreatePopupMenu();
        AppendMenuW(historyMenu, MF_STRING, kMenuHistory, L"Show History\tCtrl+H");
        AppendMenuW(historyMenu, MF_STRING, kMenuClearHistory, L"Clear History");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(historyMenu), L"History");

        bookmarksMenu = CreatePopupMenu();
        AppendMenuW(bookmarksMenu, MF_STRING, kMenuAddBookmark, L"Bookmark This Page\tCtrl+D");
        AppendMenuW(bookmarksMenu, MF_STRING, kMenuBookmarks, L"Show Bookmarks\tCtrl+Shift+B");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(bookmarksMenu), L"Bookmarks");

        AppendMenuW(menu, MF_STRING, kMenuDownloads, L"Downloads\tCtrl+I");
        AppendMenuW(menu, MF_STRING, kMenuReadingList, L"Reading List\tCtrl+Shift+D");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        AppendMenuW(menu, MF_STRING, kMenuSavePage, L"Save Page As...\tCtrl+S");
        AppendMenuW(menu, MF_STRING, kMenuPrint, L"Print...\tCtrl+P");
        AppendMenuW(menu, MF_STRING, kMenuFindInPage, L"Find in Page...\tCtrl+F");

        HMENU zoomMenu = CreatePopupMenu();
        AppendMenuW(zoomMenu, MF_STRING, kMenuZoomOut, L"Zoom Out");
        AppendMenuW(zoomMenu, MF_STRING, kMenuResetZoom, L"Actual Size");
        AppendMenuW(zoomMenu, MF_STRING, kMenuZoomIn, L"Zoom In");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(zoomMenu), L"Page Zoom");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuStartPage, L"Start Page");
        AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
        AppendMenuW(menu, MF_STRING, kMenuAbout, L"About Aurora");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit Aurora\tCtrl+Shift+Q");
    }

    void loadStartPage(TabState& tab)
    {
        auto frequent = frequentVisits(5);

        std::string html = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aurora</title>
<style>
:root { font-family:"Segoe UI",Arial,sans-serif; color-scheme:dark; }
*{box-sizing:border-box}
html,body{margin:0;min-height:100%;width:100%}
body{
 min-height:100vh;
 color:#f7fbff;
 background:
   radial-gradient(ellipse at 22% 14%,rgba(53,183,170,.28),transparent 35%),
   radial-gradient(ellipse at 76% 18%,rgba(94,88,193,.28),transparent 34%),
   radial-gradient(ellipse at 55% 80%,rgba(28,122,190,.18),transparent 42%),
   linear-gradient(145deg,#0b1722,#172b49 48%,#251e45 80%,#0a111d);
}
.page{min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:52px 24px 46px}
.brand{text-align:center}
.logo{width:92px;height:92px;margin:0 auto 18px;filter:drop-shadow(0 16px 28px rgba(0,0,0,.25))}
.logo svg{width:100%;height:100%}
.wordmark{font-size:44px;font-weight:500;letter-spacing:.03em;line-height:1}
.tag{margin-top:10px;font-size:11px;letter-spacing:.38em;color:rgba(233,244,249,.68)}
.search{width:min(760px,90vw);height:56px;margin:34px auto 30px;padding:0 22px;border:1px solid rgba(255,255,255,.30);border-radius:29px;background:rgba(255,255,255,.10);backdrop-filter:blur(18px);color:#fff;outline:none;font-size:16px;box-shadow:0 16px 45px rgba(0,0,0,.16)}
.search::placeholder{color:rgba(241,248,252,.64)}
.section{width:min(920px,94vw);text-align:left;margin-bottom:13px;color:rgba(239,247,251,.78);font-size:12px;letter-spacing:.12em;text-transform:uppercase}
.tiles{width:min(920px,94vw);display:flex;justify-content:center;flex-wrap:wrap;gap:14px}
.tile{width:150px;min-height:118px;border-radius:18px;border:1px solid rgba(255,255,255,.20);background:rgba(255,255,255,.095);backdrop-filter:blur(17px);box-shadow:0 18px 40px rgba(0,0,0,.13);display:flex;flex-direction:column;align-items:center;justify-content:center;gap:11px;color:#f2f8fb;cursor:pointer;transition:transform .12s ease,background .12s ease}
.tile:hover{transform:translateY(-2px);background:rgba(255,255,255,.145)}
.icon{width:34px;height:34px;border-radius:11px;display:grid;place-items:center;background:rgba(255,255,255,.14);font-weight:600;font-size:16px}
.name{font-size:14px}
.host{font-size:11px;color:rgba(237,247,251,.55);max-width:120px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.footer{margin-top:28px;color:rgba(235,246,250,.42);font-size:12px}
@media(max-width:700px){.tile{width:136px}.page{padding-top:35px}.wordmark{font-size:36px}}
</style>
</head>
<body>
<div class="page">
<div class="brand">
<div class="logo">
<svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
<defs>
<linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#69e7a9"/><stop offset=".48" stop-color="#27c9ad"/><stop offset="1" stop-color="#0c6d86"/></linearGradient>
<linearGradient id="g2" x1="1" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7de89e"/><stop offset=".6" stop-color="#35b9b2"/><stop offset="1" stop-color="#127994"/></linearGradient>
</defs>
<circle cx="50" cy="50" r="48" fill="url(#g)"/>
<path d="M11 42c15-24 38-34 65-28 8 2 15 5 22 11-22-3-41 8-53 27-10 16-24 16-34-10z" fill="url(#g2)" opacity=".82"/>
<path d="M11 66c17-22 36-29 57-26 10 1 18 5 23 11-19 1-33 8-44 21-10 12-26 9-36-6z" fill="#91e7a8" opacity=".64"/>
<text x="50" y="68" text-anchor="middle" font-family="Segoe UI,Arial" font-size="56" font-weight="700" fill="white">a</text>
</svg>
</div>
<div class="wordmark">aurora</div>
<div class="tag">BROWSE BEYOND</div>
</div>
<input id="search" class="search" autofocus placeholder="Search the web or enter an address">
<div class="section">)HTML";

        html += frequent.empty() ? "Quick Access" : "Frequently Visited";
        html += R"HTML(</div><div class="tiles">)HTML";

        auto addTile = [&](const std::wstring& name, const std::wstring& url, const std::wstring& host) {
            std::string safeName = toUTF8(htmlEscape(name));
            std::string safeUrl = toUTF8(htmlEscape(url));
            std::string safeHost = toUTF8(htmlEscape(host));

            char letter = '?';
            for (wchar_t ch : name) {
                if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')) {
                    letter = static_cast<char>(ch < 128 ? ch : '?');
                    break;
                }
            }

            html += "<div class=\"tile\" onclick=\"location.href='";
            html += safeUrl;
            html += "'\"><div class=\"icon\">";
            html += letter;
            html += "</div><div class=\"name\">";
            html += safeName;
            html += "</div><div class=\"host\">";
            html += safeHost;
            html += "</div></div>";
        };

        if (frequent.empty()) {
            addTile(L"YouTube", L"https://www.youtube.com", L"youtube.com");
            addTile(L"Reddit", L"https://www.reddit.com", L"reddit.com");
            addTile(L"GitHub", L"https://github.com", L"github.com");
            addTile(L"Wikipedia", L"https://www.wikipedia.org", L"wikipedia.org");
            addTile(L"Google", L"https://www.google.com", L"google.com");
        } else {
            for (const auto& entry : frequent) {
                std::wstring host = entry.url;
                size_t start = host.find(L"://");
                if (start != std::wstring::npos)
                    host.erase(0, start + 3);
                size_t slash = host.find(L'/');
                if (slash != std::wstring::npos)
                    host.erase(slash);
                addTile(
                    entry.title.empty() ? host : entry.title,
                    entry.url,
                    host
                );
            }
        }

        html += R"HTML(
<div class="tile" onclick="alert('Custom shortcuts will be added in the next browser-services pass.')"><div class="icon">+</div><div class="name">Add Shortcut</div><div class="host">Custom</div></div>
</div>
<div class="footer">Frequently visited sites are stored locally by Aurora.</div>
</div>
<script>
const search=document.getElementById('search');
search.addEventListener('keydown',e=>{
 if(e.key!=='Enter') return;
 const value=search.value.trim();
 if(!value) return;
 if(/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\//.test(value) || /^www\./i.test(value) || /^[^ \t]+\.[^ \t]+$/.test(value))
   location.href=value.includes('://')?value:'https://'+value;
 else
   location.href='https://www.google.com/search?q='+encodeURIComponent(value);
});
</script>
</body></html>
)HTML";

        auto htmlString = adoptWK(WKStringCreateWithUTF8CString(html.c_str()));
        auto baseURL = createWKURL(L"about:blank");
        WKPageLoadHTMLString(WKViewGetPage(tab.view.get()), htmlString.get(), baseURL.get());

        tab.title = L"New Tab";
        tab.activeUrl.clear();
        tab.lastRecordedUrl.clear();
        SetWindowTextW(addressBar, L"");
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

        bool hasScheme = cleaned.find(L"://") != std::wstring::npos
            || cleaned.rfind(L"about:", 0) == 0
            || cleaned.rfind(L"file:", 0) == 0
            || cleaned.rfind(L"localhost", 0) == 0
            || cleaned.rfind(L"127.0.0.1", 0) == 0;

        bool bareHost = !hasScheme
            && cleaned.find_first_of(L" \t") == std::wstring::npos
            && (cleaned.rfind(L"www.", 0) == 0 || cleaned.find(L'.') != std::wstring::npos);

        if (hasScheme)
            loadURL(*tab, cleaned);
        else if (bareHost) {
            tab->fallbackURL.clear();

            if (cleaned.rfind(L"www.", 0) == 0) {
                loadURL(*tab, L"https://" + cleaned);
            } else {
                tab->fallbackURL = L"https://" + cleaned;
                loadURL(*tab, L"https://www." + cleaned);
            }
        }
        else
            loadURL(*tab, L"https://www.google.com/search?q=" + percentEncode(cleaned));

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

        auto page = WKViewGetPage(tab->view.get());
        if (!page)
            return false;

        WKPageNavigationClientV3 navigationClient { };
        navigationClient.base.version = 3;
        navigationClient.base.clientInfo = tab.get();
        navigationClient.didFailProvisionalNavigation = didFailProvisionalNavigation;
        navigationClient.navigationActionDidBecomeDownload = navigationActionDidBecomeDownload;
        navigationClient.navigationResponseDidBecomeDownload = navigationResponseDidBecomeDownload;
        navigationClient.contextMenuDidCreateDownload = contextMenuDidCreateDownload;
        WKPageSetPageNavigationClient(page, &navigationClient.base);

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

        WKPageSetCustomBackingScaleFactor(page, windowScaleFactor(window));

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
            HWND viewWindow = WKViewGetWindow(tabs[i]->view.get());
            if (viewWindow)
                ShowWindow(viewWindow, i == activeTab ? SW_SHOW : SW_HIDE);
        }

        auto* tab = active();
        if (!tab)
            return;

        if (tab->activeUrl.empty() || tab->activeUrl == L"about:blank")
            SetWindowTextW(addressBar, L"");
        else
            SetWindowTextW(addressBar, tab->activeUrl.c_str());

        SetFocus(WKViewGetWindow(tab->view.get()));
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

        if (tabs[index]->favicon) {
            DestroyIcon(tabs[index]->favicon);
            tabs[index]->favicon = nullptr;
        }

        auto closingTab = std::move(tabs[index]);
        if (auto* viewWindow = WKViewGetWindow(closingTab->view.get()))
            ShowWindow(viewWindow, SW_HIDE);
        retiredTabs.push_back(std::move(closingTab));
        auto closingTab = std::move(tabs[index]);
        if (auto* viewWindow = WKViewGetWindow(closingTab->view.get()))
            ShowWindow(viewWindow, SW_HIDE);
        retiredTabs.push_back(std::move(closingTab));
        tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(index));

        if (activeTab >= tabs.size())
            activeTab = tabs.size() - 1;
        else if (activeTab > index)
            --activeTab;

        activateTab(activeTab);
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

    void loadLocalHTML(TabState& tab, const std::string& html)
    {
        auto htmlString = adoptWK(WKStringCreateWithUTF8CString(html.c_str()));
        auto baseURL = createWKURL(L"about:blank");
        WKPageLoadHTMLString(WKViewGetPage(tab.view.get()), htmlString.get(), baseURL.get());
    }

    std::string savedPagesHTML(const char* heading, const std::vector<SavedPage>& pages)
    {
        std::string html = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><title>Aurora</title>
<style>
body{margin:0;background:#f5f6f7;color:#202428;font-family:"Segoe UI",Arial,sans-serif}
.wrap{max-width:980px;margin:auto;padding:54px 32px}
.item{padding:16px 18px;background:#fff;border:1px solid #e1e4e6;border-radius:12px;margin:9px 0;display:block;color:inherit;text-decoration:none}
.name{font-size:15px}.url{font-size:12px;color:#70777d;margin-top:5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.empty{padding:22px;color:#777}
</style></head><body><main class="wrap"><h1>)HTML";
        html += heading;
        html += R"HTML(</h1>)HTML";

        if (pages.empty()) {
            html += "<div class=\"empty\">Nothing here yet.</div>";
        } else {
            for (const auto& page : pages) {
                html += "<a class=\"item\" href=\"" + toUTF8(htmlEscape(page.url)) + "\"><div class=\"name\">";
                html += toUTF8(htmlEscape(page.title.empty() ? page.url : page.title));
                html += "</div><div class=\"url\">";
                html += toUTF8(htmlEscape(page.url));
                html += "</div></a>";
            }
        }

        html += R"HTML(</main></body></html>)HTML";
        return html;
    }

    void loadBookmarksPage()
    {
        if (auto* tab = active())
            loadLocalHTML(*tab, savedPagesHTML("Bookmarks", bookmarks));
    }

    void loadReadingListPage()
    {
        if (auto* tab = active())
            loadLocalHTML(*tab, savedPagesHTML("Reading List", readingList));
    }

    void showFileInFolder(const std::wstring& path)
    {
        if (path.empty())
            return;

        std::wstring arguments = L"/select,\"" + path + L"\"";
        ShellExecuteW(window, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void showDownloadsMenu()
    {
        HMENU downloadsMenu = CreatePopupMenu();
        if (!downloadsMenu)
            return;

        if (downloads.empty()) {
            AppendMenuW(downloadsMenu, MF_GRAYED | MF_STRING, 5998, L"No downloads yet");
        } else {
            size_t first = downloads.size() > 8 ? downloads.size() - 8 : 0;
            for (size_t i = downloads.size(); i-- > first;) {
                std::wstring label = downloads[i].failed ? L"Failed — " : L"";
                label += downloads[i].filename;
                label += L"  —  Show in Folder";
                AppendMenuW(downloadsMenu, MF_STRING, 6000 + static_cast<UINT>(i), label.c_str());
            }
            AppendMenuW(downloadsMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(downloadsMenu, MF_STRING, 5999, L"Show Latest in Folder");
        }

        AppendMenuW(downloadsMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(downloadsMenu, MF_STRING, 5997, L"Open Downloads Folder");

        RECT client { };
        GetClientRect(window, &client);
        POINT point {
            client.right - scaleForDpi(window, kToolbarHorizontalPadding) - scaleForDpi(window, kToolbarButtonSize) / 2,
            titleBarHeight() + toolbarHeight()
        };
        ClientToScreen(window, &point);
        TrackPopupMenu(downloadsMenu, TPM_RIGHTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
        DestroyMenu(downloadsMenu);
    }

    void loadHistoryPage()
    {
        auto* tab = active();
        if (!tab)
            return;

        auto sorted = visits;
        std::sort(sorted.begin(), sorted.end(), [](const VisitEntry& a, const VisitEntry& b) {
            return a.lastVisited > b.lastVisited;
        });

        std::string html = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><title>Aurora History</title>
<style>
body{margin:0;background:#f5f6f7;color:#202428;font-family:"Segoe UI",Arial,sans-serif}
.item{padding:15px 18px;background:#fff;border:1px solid #e1e4e6;border-radius:12px;margin:8px 0}
.name{font-size:15px}.meta{font-size:12px;color:#73777d;margin-top:5px}
.item a{color:inherit;text-decoration:none}.empty{padding:22px;color:#777}
</style></head><body><main style="max-width:980px;margin:auto;padding:54px 32px"><h1>History</h1>)HTML";

        if (sorted.empty()) {
            html += "<div class=\"empty\">No browsing history.</div>";
        } else {
            for (const auto& entry : sorted) {
                html += "<div class=\"item\"><a href=\"" + toUTF8(htmlEscape(entry.url)) + "\"><div class=\"name\">";
                html += toUTF8(htmlEscape(entry.title.empty() ? entry.url : entry.title));
                html += "</div><div class=\"meta\">Visited " + std::to_string(entry.visits);
                html += entry.visits == 1 ? " time" : " times";
                html += "</div></a></div>";
            }
        }

        html += "</main></body></html>";
        loadLocalHTML(*tab, html);
    }

    void toggleBookmark()
    {
        auto* tab = active();
        if (!tab || tab->activeUrl.empty() || tab->activeUrl == L"about:blank")
            return;

        auto found = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const SavedPage& page) {
            return page.url == tab->activeUrl;
        });

        if (found != bookmarks.end())
            bookmarks.erase(found);
        else
            bookmarks.push_back({ tab->activeUrl, tab->title.empty() ? tab->activeUrl : tab->title, currentUnixTime() });

        saveSavedPages(bookmarksPath(), bookmarks);
        InvalidateRect(window, nullptr, TRUE);
    }

    void addReadingList()
    {
        auto* tab = active();
        if (!tab || tab->activeUrl.empty() || tab->activeUrl == L"about:blank")
            return;

        auto found = std::find_if(readingList.begin(), readingList.end(), [&](const SavedPage& page) {
            return page.url == tab->activeUrl;
        });

        if (found == readingList.end())
            readingList.push_back({ tab->activeUrl, tab->title.empty() ? tab->activeUrl : tab->title, currentUnixTime() });

        saveSavedPages(readingListPath(), readingList);
        InvalidateRect(window, nullptr, TRUE);
    }

    void clearHistory()
    {
        visits.clear();
        saveVisitData();
        loadHistoryPage();
    }

    void showMenu()
    {
        RECT client { };
        GetClientRect(window, &client);

        int button = scaleForDpi(window, kToolbarButtonSize);
        int pad = scaleForDpi(window, kToolbarHorizontalPadding);
        POINT point {
            client.right - pad - button / 2,
            titleBarHeight() + toolbarHeight()
        };
        ClientToScreen(window, &point);

        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
    }

    RECT addressPillRect() const
    {
        RECT client { };
        GetClientRect(window, &client);

        int pad = scaleForDpi(window, kToolbarHorizontalPadding);
        int button = scaleForDpi(window, kToolbarButtonSize);
        int gap = scaleForDpi(window, kToolbarGap);

        int leftControls = button * 3 + gap * 2;
        int rightControls = button * 3 + gap * 4;

        int left = pad + leftControls + scaleForDpi(window, 12);
        int right = client.right - pad - rightControls;

        return {
            left,
            titleBarHeight() + scaleForDpi(window, 7),
            std::max(left + scaleForDpi(window, 220), right),
            titleBarHeight() + toolbarHeight() - scaleForDpi(window, 7)
        };
    }

    void resize() const
    {
        if (!window)
            return;

        RECT client { };
        if (!GetClientRect(window, &client))
            return;

        RECT pill = addressPillRect();
        MoveWindow(
            addressBar,
            pill.left + scaleForDpi(window, 30),
            pill.top + scaleForDpi(window, 2),
            std::max<int>(scaleForDpi(window, 140), static_cast<int>(pill.right - pill.left - scaleForDpi(window, 62))),
            std::max<int>(scaleForDpi(window, 24), static_cast<int>(pill.bottom - pill.top - scaleForDpi(window, 4))),
            TRUE
        );

        int contentTop = titleBarHeight() + toolbarHeight();
        int contentLeft = sidebarOpen ? scaleForDpi(window, 286) : 0;
        for (const auto& tab : tabs) {
            HWND viewWindow = WKViewGetWindow(tab->view.get());
            if (!viewWindow)
                continue;

            MoveWindow(
                viewWindow,
                contentLeft,
                contentTop,
                std::max<int>(0, static_cast<int>(client.right - contentLeft)),
                std::max<int>(0, static_cast<int>(client.bottom - contentTop)),
                TRUE
            );
        }

        InvalidateRect(window, nullptr, TRUE);
    }

    void toggleMaximizeWindow()
    {
        if (!customMaximized) {
            GetWindowRect(window, &restoredWindowRect);

            HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info { };
            info.cbSize = sizeof(info);
            if (monitor && GetMonitorInfoW(monitor, &info)) {
                SetWindowPos(
                    window,
                    HWND_TOP,
                    info.rcWork.left,
                    info.rcWork.top,
                    info.rcWork.right - info.rcWork.left,
                    info.rcWork.bottom - info.rcWork.top,
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
                );
                customMaximized = true;
            }
        } else {
            SetWindowPos(
                window,
                HWND_TOP,
                restoredWindowRect.left,
                restoredWindowRect.top,
                restoredWindowRect.right - restoredWindowRect.left,
                restoredWindowRect.bottom - restoredWindowRect.top,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
            );
            customMaximized = false;
        }

        resize();
    }

    void handleCommand(UINT command)
    {
        if (command >= 6000 && command < 6000 + downloads.size()) {
            size_t index = command - 6000;
            if (index < downloads.size())
                showFileInFolder(downloads[index].path);
            return;
        }

        if (command >= 3000 && command < 3000 + tabs.size()) {
            activateTab(command - 3000);
            return;
        }

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
            sidebarOpen = !sidebarOpen;
            resize();
            InvalidateRect(window, nullptr, TRUE);
            break;
        case kCommandPrivacy:
            MessageBoxW(window, L"Privacy controls will be connected here in the privacy-services pass.", L"Privacy — Aurora", MB_OK);
            break;
        case kCommandReader:
            if (auto* tab = active()) {
                auto js = adoptWK(WKStringCreateWithUTF8CString("(function(){const id=\'aurora-reader-mode\';const old=document.getElementById(id);if(old){old.remove();return;}const s=document.createElement(\'style\');s.id=id;s.textContent=\'body{background:#f7f3ea!important;color:#252525!important;font-family:Georgia,Times New Roman,serif!important;font-size:20px!important;line-height:1.75!important}main,article,[role=main]{max-width:760px!important;margin:40px auto!important;padding:0 24px!important}\';document.head.appendChild(s);})()"));
                WKPageEvaluateJavaScriptInMainFrame(WKViewGetPage(tab->view.get()), js.get(), nullptr, nullptr);
            }
            break;
        case kCommandNewTab:
            addTab(true);
            break;
        case kCommandMenu:
            showMenu();
            break;
        case kCommandAddress:
            SetFocus(addressBar);
            SendMessageW(addressBar, EM_SETSEL, 0, -1);
            break;
        case kCommandCloseTab:
            closeActiveTab();
            break;
        case kCommandShare:
            MessageBoxW(window, L"Share is reserved for the next browser-services pass.", L"Aurora", MB_OK);
            break;
        case kCommandDownloads:
            showDownloadsMenu();
            break;
        case kCommandMinimize:
            ShowWindow(window, SW_MINIMIZE);
            break;
        case kCommandMaximize:
            toggleMaximizeWindow();
            break;
        case kCommandCloseWindow:
            PostMessageW(window, WM_CLOSE, 0, 0);
            break;
        case kMenuNewTab:
            addTab(true);
            break;
        case kMenuNewWindow:
            MessageBoxW(window, L"New Window will be enabled in the next browser-core pass.", L"Aurora", MB_OK);
            break;
        case kMenuNewPrivateWindow:
            MessageBoxW(window, L"Private browsing is planned next. This entry is reserved for the private data-store implementation.", L"Aurora", MB_OK);
            break;
        case kMenuHistory:
            loadHistoryPage();
            break;
        case kMenuClearHistory:
            clearHistory();
            break;
        case kMenuAddBookmark:
            toggleBookmark();
            break;
        case kMenuBookmarks:
            loadBookmarksPage();
            break;
        case kMenuReadingList:
            loadReadingListPage();
            break;
        case kMenuDownloads:
            showDownloadsMenu();
            break;
        case kMenuSavePage:
            MessageBoxW(window, L"Save Page As will be connected to WebKit downloads in the next browser-services pass.", L"Aurora", MB_OK);
            break;
        case kMenuPrint:
            MessageBoxW(window, L"Printing will be connected to the Windows print pipeline in a later pass.", L"Aurora", MB_OK);
            break;
        case kMenuFindInPage:
            MessageBoxW(window, L"Find in Page will be added in the next page-services pass.", L"Aurora", MB_OK);
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
        case kMenuStartPage:
            if (auto* tab = active())
                loadStartPage(*tab);
            break;
        case kMenuSettings:
            MessageBoxW(window, L"Aurora Settings will be added here. This will control appearance, privacy, search, and start-page behavior.", L"Settings — Aurora", MB_OK);
            break;
        case kMenuAbout:
            MessageBoxW(
                window,
                L"Aurora\n\nA WebKit browser for Windows.\n\nSafari-inspired interface with original Aurora branding.",
                L"About Aurora",
                MB_OK | MB_ICONINFORMATION
            );
            break;
        case 5997: {
            std::wstring folder = downloadsDirectory();
            if (!folder.empty())
                ShellExecuteW(window, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case 5999:
            if (!downloads.empty())
                showFileInFolder(downloads.back().path);
            break;
        case kMenuQuit:
            PostMessageW(window, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }

        InvalidateRect(window, nullptr, TRUE);
    }

    void closeActiveTab()
    {
        closeTab(activeTab);
    }

    bool topBarHasInteractiveHit(POINT point) const
    {
        int dpi = windowDpi(window);
        int titleH = MulDiv(kTitleBarHeight, dpi, 96);
        int pad = MulDiv(kToolbarHorizontalPadding, dpi, 96);
        int light = MulDiv(kTrafficLightSize, dpi, 96);
        int lightGap = MulDiv(kTrafficLightGap, dpi, 96);
        int button = MulDiv(kToolbarButtonSize, dpi, 96);
        int gap = MulDiv(kToolbarGap, dpi, 96);

        if (point.y < 0 || point.y >= titleH)
            return false;

        RECT closeRect { pad, (titleH - light) / 2, pad + light, (titleH + light) / 2 };
        RECT minimizeRect {
            pad + light + lightGap,
            (titleH - light) / 2,
            pad + light * 2 + lightGap,
            (titleH + light) / 2
        };
        RECT maximizeRect {
            pad + light * 2 + lightGap * 2,
            (titleH - light) / 2,
            pad + light * 3 + lightGap * 2,
            (titleH + light) / 2
        };

        if (PtInRect(&closeRect, point) || PtInRect(&minimizeRect, point) || PtInRect(&maximizeRect, point))
            return true;

        int tabsStart = pad + light * 3 + lightGap * 4 + MulDiv(26, dpi, 96);
        int plusLeft = GetClientWidth(window) - pad - button;
        int tabCount = std::max(1, static_cast<int>(tabs.size()));
        int available = plusLeft - tabsStart - pad - gap * (tabCount + 1);
        int tabWidth = std::clamp(
            available / tabCount,
            MulDiv(kTabMinWidth, dpi, 96),
            MulDiv(kTabMaxWidth, dpi, 96)
        );

        for (size_t i = 0; i < tabs.size(); ++i) {
            int left = tabsStart + static_cast<int>(i) * (tabWidth + gap);
            RECT tabRect { left, MulDiv(4, dpi, 96), left + tabWidth, titleH - MulDiv(4, dpi, 96) };
            if (PtInRect(&tabRect, point))
                return true;
        }

        RECT plusRect { plusLeft, 0, plusLeft + button, titleH };
        return PtInRect(&plusRect, point);
    }

    bool handleTopBarHit(POINT point)
    {
        int dpi = windowDpi(window);
        int titleH = MulDiv(kTitleBarHeight, dpi, 96);
        int pad = MulDiv(kToolbarHorizontalPadding, dpi, 96);
        int light = MulDiv(kTrafficLightSize, dpi, 96);
        int lightGap = MulDiv(kTrafficLightGap, dpi, 96);

        if (point.y >= 0 && point.y < titleH) {
            int controlHit = scaleForDpi(window, 20);
            int controlY = (titleH - controlHit) / 2;
            RECT closeRect { pad - scaleForDpi(window, 2), controlY, pad - scaleForDpi(window, 2) + controlHit, controlY + controlHit };
            RECT minimizeRect {
                pad + light + lightGap - scaleForDpi(window, 2),
                controlY,
                pad + light + lightGap - scaleForDpi(window, 2) + controlHit,
                controlY + controlHit
            };
            RECT maximizeRect {
                pad + light * 2 + lightGap * 2 - scaleForDpi(window, 2),
                controlY,
                pad + light * 2 + lightGap * 2 - scaleForDpi(window, 2) + controlHit,
                controlY + controlHit
            };

            if (PtInRect(&closeRect, point)) {
                handleCommand(kCommandCloseWindow);
                return true;
            }
            if (PtInRect(&minimizeRect, point)) {
                handleCommand(kCommandMinimize);
                return true;
            }
            if (PtInRect(&maximizeRect, point)) {
                handleCommand(kCommandMaximize);
                return true;
            }

            int tabsStart = pad + light * 3 + lightGap * 4 + MulDiv(26, dpi, 96);
            int button = MulDiv(kToolbarButtonSize, dpi, 96);
            int plusGap = MulDiv(kToolbarGap, dpi, 96);

            int plusLeft = GetClientWidth(window) - pad - button;
            int tabCount = std::max(1, static_cast<int>(tabs.size()));
            int available = plusLeft - tabsStart - pad - plusGap * (tabCount + 1);
            int tabWidth = std::clamp(
                available / tabCount,
                MulDiv(kTabMinWidth, dpi, 96),
                MulDiv(kTabMaxWidth, dpi, 96)
            );

            for (size_t i = 0; i < tabs.size(); ++i) {
                int left = tabsStart + static_cast<int>(i) * (tabWidth + plusGap);
                if (point.x >= left && point.x < left + tabWidth)
                    return handleTabHit(point, i, left, tabWidth);
            }

            if (point.x >= plusLeft && point.x < plusLeft + button) {
                handleCommand(kCommandNewTab);
                return true;
            }

            return false;
        }

        return false;
    }

    bool handleToolbarHit(POINT point)
    {
        int dpi = windowDpi(window);
        int titleH = MulDiv(kTitleBarHeight, dpi, 96);
        int toolbarH = MulDiv(kToolbarHeight, dpi, 96);
        int button = MulDiv(kToolbarButtonSize, dpi, 96);
        int gap = MulDiv(kToolbarGap, dpi, 96);
        int pad = MulDiv(kToolbarHorizontalPadding, dpi, 96);

        if (point.y < titleH || point.y >= titleH + toolbarH)
            return false;

        int x = pad;
        if (point.x >= x && point.x < x + button) {
            handleCommand(kCommandBack);
            return true;
        }
        x += button + gap;
        if (point.x >= x && point.x < x + button) {
            handleCommand(kCommandForward);
            return true;
        }
        x += button + gap;
        if (point.x >= x && point.x < x + button) {
            handleCommand(kCommandSidebar);
            return true;
        }

        RECT pill = addressPillRect();
        RECT privacyRect { pill.left, pill.top, pill.left + scaleForDpi(window, 30), pill.bottom };
        if (PtInRect(&privacyRect, point)) {
            handleCommand(kCommandPrivacy);
            return true;
        }
        RECT reloadRect { pill.right - scaleForDpi(window, 32), pill.top, pill.right, pill.bottom };
        if (PtInRect(&reloadRect, point)) {
            handleCommand(kCommandReload);
            return true;
        }

        int rightStart = pill.right + gap * 2;
        RECT shareRect { rightStart, titleH, rightStart + button, titleH + toolbarH };
        RECT downloadsRect { shareRect.right + gap, titleH, shareRect.right + gap + button, titleH + toolbarH };
        RECT menuRect { downloadsRect.right + gap, titleH, downloadsRect.right + gap + button, titleH + toolbarH };

        if (PtInRect(&pageMenuRect, point)) {
            handleCommand(kCommandMenu);
            return true;
        }
        if (PtInRect(&reloadRect, point)) {
            handleCommand(kCommandReload);
            return true;
        }
        if (PtInRect(&shareRect, point)) {
            handleCommand(kCommandShare);
            return true;
        }
        if (PtInRect(&downloadsRect, point)) {
            handleCommand(kCommandDownloads);
            return true;
        }
        if (PtInRect(&menuRect, point)) {
            handleCommand(kCommandMenu);
            return true;
        }

        return false;
    }

    bool handleTabHit(POINT point, size_t index, int left, int tabWidth)
    {
        int closeWidth = scaleForDpi(window, 28);
        if (point.x >= left + tabWidth - closeWidth) {
            closeTab(index);
            return true;
        }

        activateTab(index);
        return true;
    }

    static int GetClientWidth(HWND hwnd)
    {
        RECT rect { };
        GetClientRect(hwnd, &rect);
        return rect.right;
    }

    void drawSidebarPanel(HDC dc)
    {
        if (!sidebarOpen)
            return;

        int width = scaleForDpi(window, 286);
        int top = titleBarHeight() + toolbarHeight();
        RECT client { };
        GetClientRect(window, &client);

        RECT panel { 0, top, width, client.bottom };
        HBRUSH brush = CreateSolidBrush(RGB(247, 248, 249));
        FillRect(dc, &panel, brush);
        DeleteObject(brush);

        HPEN divider = CreatePen(PS_SOLID, 1, RGB(220, 223, 225));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, divider));
        MoveToEx(dc, width - 1, top, nullptr);
        LineTo(dc, width - 1, client.bottom);
        SelectObject(dc, oldPen);
        DeleteObject(divider);

        int dpi = windowDpi(window);
        HFONT font = CreateFontW(-MulDiv(15, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
        SetBkMode(dc, TRANSPARENT);

        RECT heading { scaleForDpi(window, 18), top + scaleForDpi(window, 14),
            width - scaleForDpi(window, 54), top + scaleForDpi(window, 44) };
        SetTextColor(dc, RGB(35, 39, 42));
        const wchar_t* headingText = sidebarSection == 0 ? L"Bookmarks" : (sidebarSection == 1 ? L"Reading List" : L"History");
        DrawTextW(dc, headingText, -1, &heading, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT closeRect { width - scaleForDpi(window, 48), top + scaleForDpi(window, 10),
            width - scaleForDpi(window, 10), top + scaleForDpi(window, 44) };
        drawClose(dc, closeRect);

        RECT selector { scaleForDpi(window, 12), top + scaleForDpi(window, 52),
            width - scaleForDpi(window, 12), top + scaleForDpi(window, 84) };
        int part = (selector.right - selector.left) / 3;
        for (int i = 0; i < 3; ++i) {
            RECT r { selector.left + part * i, selector.top, selector.left + part * (i + 1), selector.bottom };
            if (i == sidebarSection) {
                HBRUSH selected = CreateSolidBrush(RGB(229, 232, 235));
                FillRect(dc, &r, selected);
                DeleteObject(selected);
            }
            SetTextColor(dc, i == sidebarSection ? RGB(35, 39, 42) : RGB(115, 120, 124));
            const wchar_t* label = i == 0 ? L"Bookmarks" : (i == 1 ? L"Reading List" : L"History");
            DrawTextW(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        int rowTop = top + scaleForDpi(window, 100);
        int rowHeight = scaleForDpi(window, 62);
        size_t count = sidebarSection == 0 ? bookmarks.size() : (sidebarSection == 1 ? readingList.size() : visits.size());

        if (!count) {
            RECT empty { scaleForDpi(window, 18), rowTop, width - scaleForDpi(window, 18), rowTop + scaleForDpi(window, 42) };
            SetTextColor(dc, RGB(125, 130, 134));
            const wchar_t* emptyText = sidebarSection == 0 ? L"No bookmarks yet." : (sidebarSection == 1 ? L"No saved pages yet." : L"No browsing history.");
            DrawTextW(dc, emptyText, -1, &empty, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        } else {
            SetTextColor(dc, RGB(42, 46, 49));
            if (sidebarSection == 0) {
                for (size_t i = 0; i < std::min<size_t>(10, bookmarks.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 6), width - scaleForDpi(window, 16), y + scaleForDpi(window, 28) };
                    DrawTextW(dc, bookmarks[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 31), width - scaleForDpi(window, 16), y + scaleForDpi(window, 50) };
                    SetTextColor(dc, RGB(126, 131, 135));
                    DrawTextW(dc, bookmarks[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SetTextColor(dc, RGB(42, 46, 49));
                }
            } else if (sidebarSection == 1) {
                for (size_t i = 0; i < std::min<size_t>(10, readingList.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 6), width - scaleForDpi(window, 16), y + scaleForDpi(window, 28) };
                    DrawTextW(dc, readingList[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 31), width - scaleForDpi(window, 16), y + scaleForDpi(window, 50) };
                    SetTextColor(dc, RGB(126, 131, 135));
                    DrawTextW(dc, readingList[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SetTextColor(dc, RGB(42, 46, 49));
                }
            } else {
                auto sorted = visits;
                std::sort(sorted.begin(), sorted.end(), [](const VisitEntry& a, const VisitEntry& b) { return a.lastVisited > b.lastVisited; });
                for (size_t i = 0; i < std::min<size_t>(10, sorted.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 6), width - scaleForDpi(window, 16), y + scaleForDpi(window, 28) };
                    DrawTextW(dc, sorted[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 31), width - scaleForDpi(window, 16), y + scaleForDpi(window, 50) };
                    SetTextColor(dc, RGB(126, 131, 135));
                    DrawTextW(dc, sorted[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SetTextColor(dc, RGB(42, 46, 49));
                }
            }
        }

        SelectObject(dc, oldFont);
        DeleteObject(font);
    }

    bool handleSidebarHit(POINT point)
    {
        if (!sidebarOpen)
            return false;

        int width = scaleForDpi(window, 286);
        int top = titleBarHeight() + toolbarHeight();
        if (point.x < 0 || point.x >= width || point.y < top)
            return false;

        RECT closeRect { width - scaleForDpi(window, 52), top + scaleForDpi(window, 8),
            width - scaleForDpi(window, 6), top + scaleForDpi(window, 48) };
        if (PtInRect(&closeRect, point)) {
            sidebarOpen = false;
            resize();
            InvalidateRect(window, nullptr, TRUE);
            return true;
        }

        RECT selector { scaleForDpi(window, 12), top + scaleForDpi(window, 52),
            width - scaleForDpi(window, 12), top + scaleForDpi(window, 84) };
        int part = (selector.right - selector.left) / 3;
        for (int i = 0; i < 3; ++i) {
            RECT r { selector.left + part * i, selector.top, selector.left + part * (i + 1), selector.bottom };
            if (PtInRect(&r, point)) {
                sidebarSection = i;
                InvalidateRect(window, nullptr, TRUE);
                return true;
            }
        }

        int rowTop = top + scaleForDpi(window, 100);
        int rowHeight = scaleForDpi(window, 62);
        int index = (point.y - rowTop) / rowHeight;
        if (index < 0)
            return true;

        auto* tab = active();
        if (!tab)
            return true;

        if (sidebarSection == 0 && static_cast<size_t>(index) < bookmarks.size()) {
            loadURL(*tab, bookmarks[static_cast<size_t>(index)].url);
            return true;
        }

        if (sidebarSection == 1 && static_cast<size_t>(index) < readingList.size()) {
            loadURL(*tab, readingList[static_cast<size_t>(index)].url);
            return true;
        }

        if (sidebarSection == 2) {
            auto sorted = visits;
            std::sort(sorted.begin(), sorted.end(), [](const VisitEntry& a, const VisitEntry& b) { return a.lastVisited > b.lastVisited; });
            if (static_cast<size_t>(index) < sorted.size()) {
                loadURL(*tab, sorted[static_cast<size_t>(index)].url);
                return true;
            }
        }

        return true;
    }

    void paint(HDC dc)
    {
        RECT client { };
        GetClientRect(window, &client);

        int dpi = windowDpi(window);
        int titleH = MulDiv(kTitleBarHeight, dpi, 96);
        int toolbarH = MulDiv(kToolbarHeight, dpi, 96);
        int button = MulDiv(kToolbarButtonSize, dpi, 96);
        int gap = MulDiv(kToolbarGap, dpi, 96);
        int pad = MulDiv(kToolbarHorizontalPadding, dpi, 96);

        HBRUSH pageBrush = CreateSolidBrush(RGB(249, 249, 250));
        FillRect(dc, &client, pageBrush);
        DeleteObject(pageBrush);

        HBRUSH titleBrush = CreateSolidBrush(RGB(241, 242, 243));
        RECT titleRect { 0, 0, client.right, titleH };
        FillRect(dc, &titleRect, titleBrush);
        DeleteObject(titleBrush);

        HBRUSH toolbarBrush = CreateSolidBrush(RGB(246, 247, 248));
        RECT toolbarRect { 0, titleH, client.right, titleH + toolbarH };
        FillRect(dc, &toolbarRect, toolbarBrush);
        DeleteObject(toolbarBrush);

        drawSidebarPanel(dc);

        int light = MulDiv(kTrafficLightSize, dpi, 96);
        int lightGap = MulDiv(kTrafficLightGap, dpi, 96);
        int lightY = (titleH - light) / 2;

        drawTrafficLight(dc, pad, lightY, RGB(255, 95, 87));
        drawTrafficLight(dc, pad + light + lightGap, lightY, RGB(255, 189, 46));
        drawTrafficLight(dc, pad + light * 2 + lightGap * 2, lightY, RGB(39, 201, 63));

        int tabsStart = pad + light * 3 + lightGap * 4 + MulDiv(26, dpi, 96);
        int plusLeft = client.right - pad - button;
        int tabCount = std::max(1, static_cast<int>(tabs.size()));
        int available = plusLeft - tabsStart - pad - gap * (tabCount + 1);
        int tabWidth = std::clamp(
            available / tabCount,
            MulDiv(kTabMinWidth, dpi, 96),
            MulDiv(kTabMaxWidth, dpi, 96)
        );

        for (size_t i = 0; i < tabs.size(); ++i) {
            int left = tabsStart + static_cast<int>(i) * (tabWidth + gap);
            RECT tabRect {
                left,
                MulDiv(3, dpi, 96),
                left + tabWidth,
                titleH - MulDiv(3, dpi, 96)
            };

            HBRUSH tabBrush = CreateSolidBrush(i == activeTab ? RGB(250, 251, 251) : RGB(239, 240, 241));
            HPEN tabPen = CreatePen(PS_SOLID, 1, i == activeTab ? RGB(211, 214, 216) : RGB(229, 231, 232));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, tabBrush));
            HPEN oldPen = static_cast<HPEN>(SelectObject(dc, tabPen));
            RoundRect(dc, tabRect.left, tabRect.top, tabRect.right, tabRect.bottom, MulDiv(8, dpi, 96), MulDiv(8, dpi, 96));
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(tabBrush);
            DeleteObject(tabPen);

            int iconSize = MulDiv(16, dpi, 96);
            int iconTop = tabRect.top + (tabRect.bottom - tabRect.top - iconSize) / 2;
            RECT iconRect {
                tabRect.left + MulDiv(9, dpi, 96),
                iconTop,
                tabRect.left + MulDiv(9, dpi, 96) + iconSize,
                iconTop + iconSize
            };
            if (tabs[i]->favicon)
                DrawIconEx(dc, iconRect.left, iconRect.top, tabs[i]->favicon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            else
                drawAuroraMark(dc, iconRect, false);

            RECT titleText = tabRect;
            titleText.left = iconRect.right + MulDiv(6, dpi, 96);
            titleText.right -= MulDiv(31, dpi, 96);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(45, 49, 52));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dc, uiFont));
            DrawTextW(dc, tabs[i]->title.c_str(), -1, &titleText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, oldFont);

            RECT closeRect {
                tabRect.right - MulDiv(26, dpi, 96),
                tabRect.top,
                tabRect.right - MulDiv(2, dpi, 96),
                tabRect.bottom
            };
            drawClose(dc, closeRect);
        }

        RECT plusRect { plusLeft, 0, plusLeft + button, titleH };
        drawPlus(dc, plusRect);

        int x = pad;
        RECT backRect { x, titleH, x + button, titleH + toolbarH };
        drawChevron(dc, backRect, false, active() && WKPageCanGoBack(WKViewGetPage(active()->view.get())));
        x += button + gap;
        RECT forwardRect { x, titleH, x + button, titleH + toolbarH };
        drawChevron(dc, forwardRect, true, active() && WKPageCanGoForward(WKViewGetPage(active()->view.get())));
        x += button + gap;
        RECT sidebarRect { x, titleH, x + button, titleH + toolbarH };
        drawSidebar(dc, sidebarRect);

        RECT pill = addressPillRect();
        HBRUSH pillBrush = CreateSolidBrush(RGB(245, 246, 247));
        HPEN pillPen = CreatePen(PS_SOLID, 1, RGB(208, 212, 214));
        HBRUSH oldPillBrush = static_cast<HBRUSH>(SelectObject(dc, pillBrush));
        HPEN oldPillPen = static_cast<HPEN>(SelectObject(dc, pillPen));
        RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom, MulDiv(11, dpi, 96), MulDiv(11, dpi, 96));
        SelectObject(dc, oldPillBrush);
        SelectObject(dc, oldPillPen);
        DeleteObject(pillBrush);
        DeleteObject(pillPen);

        // Compact smart-search/privacy/reader glyphs.
        HPEN searchPen = CreatePen(PS_SOLID, 1, RGB(83, 88, 91));
        HPEN oldSearchPen = static_cast<HPEN>(SelectObject(dc, searchPen));
        int sx = pill.left + MulDiv(18, dpi, 96);
        int sy = (pill.top + pill.bottom) / 2;
        Ellipse(dc, sx - 5, sy - 5, sx + 5, sy + 5);
        MoveToEx(dc, sx + 4, sy + 4, nullptr);
        LineTo(dc, sx + 8, sy + 8);
        SelectObject(dc, oldSearchPen);
        DeleteObject(searchPen);

        RECT privacyGlyph { pill.left + MulDiv(2, dpi, 96), pill.top, pill.left + MulDiv(30, dpi, 96), pill.bottom };
        drawShield(dc, privacyGlyph);
        bool loading = active() && WKPageGetEstimatedProgress(WKViewGetPage(active()->view.get())) < 1.0;
        RECT reloadRect { pill.right - MulDiv(30, dpi, 96), pill.top, pill.right, pill.bottom };
        drawReload(dc, reloadRect, loading);

        int rightStart = pill.right + gap * 2;
        RECT shareRect { rightStart, titleH, rightStart + button, titleH + toolbarH };
        RECT downloadsRect { shareRect.right + gap, titleH, shareRect.right + gap + button, titleH + toolbarH };
        RECT menuRect { downloadsRect.right + gap, titleH, downloadsRect.right + gap + button, titleH + toolbarH };

        drawShare(dc, shareRect);
        drawDownload(dc, downloadsRect);
        drawMenu(dc, menuRect);
    }
};

void requestFavicon(BrowserState* browser, const std::wstring& pageURL)
{
    if (!browser || pageURL.empty())
        return;

    const std::wstring faviconURL = faviconURLForPage(pageURL);
    if (faviconURL.empty())
        return;

    HWND window = browser->window;
    std::thread([window, pageURL, faviconURL] {
        wchar_t cacheFile[MAX_PATH] { };
        HRESULT hr = URLDownloadToCacheFileW(nullptr, faviconURL.c_str(), cacheFile, MAX_PATH, 0, nullptr);
        if (FAILED(hr) || !cacheFile[0] || !IsWindow(window))
            return;

        HICON icon = static_cast<HICON>(LoadImageW(nullptr, cacheFile, IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (!icon)
            return;

        auto* result = new FaviconResult { pageURL, icon };
        if (!PostMessageW(window, WM_AURORA_FAVICON_READY, 0, reinterpret_cast<LPARAM>(result))) {
            DestroyIcon(icon);
            delete result;
        }
    }).detach();
}

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

void didFailProvisionalNavigation(WKPageRef, WKNavigationRef, WKErrorRef, WKTypeRef, const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (!tab || !tab->browser || tab->fallbackURL.empty())
        return;

    auto fallback = tab->fallbackURL;
    tab->fallbackURL.clear();
    tab->browser->loadURL(*tab, fallback);
}

WKStringRef downloadDecideDestinationWithResponse(WKDownloadRef, WKURLResponseRef response, WKStringRef suggestedFilename, const void* clientInfo)
{
    std::wstring filename = createString(suggestedFilename);

    if (filename.empty()) {
        auto responseURL = adoptWK(WKURLResponseCopyURL(response));
        filename = createString(adoptWK(WKURLCopyHostName(responseURL.get())).get());
    }

    if (filename.empty())
        filename = L"download";

    for (auto& ch : filename) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            ch = L'_';
    }

    std::wstring folder = downloadsDirectory();
    if (folder.empty())
        return nullptr;

    std::wstring path = folder + L"\\" + filename;

    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (browser) {
        browser->downloads.push_back({ filename, path, false });
        if (browser->downloads.size() > 32)
            browser->downloads.erase(browser->downloads.begin());
        InvalidateRect(browser->window, nullptr, TRUE);
    }

    std::string utf8 = toUTF8(path);
    return WKStringCreateWithUTF8CString(utf8.c_str());
}

void downloadDidFinish(WKDownloadRef, const void*)
{
}

void downloadDidFailWithError(WKDownloadRef, WKErrorRef error, WKDataRef, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return;

    if (!browser->downloads.empty())
        browser->downloads.back().failed = true;

    std::wstring description = createString(adoptWK(WKErrorCopyLocalizedDescription(error)).get());
    MessageBoxW(browser->window, description.c_str(), L"Download Failed — Aurora", MB_OK | MB_ICONWARNING);
}

void installDownloadClient(WKDownloadRef download, BrowserState* browser)
{
    if (!download || !browser)
        return;

    WKDownloadClientV0 client { };
    client.base.version = 0;
    client.base.clientInfo = browser;
    client.decideDestinationWithResponse = downloadDecideDestinationWithResponse;
    client.didFinish = downloadDidFinish;
    client.didFailWithError = downloadDidFailWithError;
    WKDownloadSetClient(download, &client.base);
}

void navigationActionDidBecomeDownload(WKPageRef, WKNavigationActionRef, WKDownloadRef download, const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab)
        installDownloadClient(download, tab->browser);
}

void navigationResponseDidBecomeDownload(WKPageRef, WKNavigationResponseRef, WKDownloadRef download, const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab)
        installDownloadClient(download, tab->browser);
}

void contextMenuDidCreateDownload(WKPageRef, WKDownloadRef download, const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab)
        installDownloadClient(download, tab->browser);
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
    if (!tab || !tab->browser)
        return;

    auto page = WKViewGetPage(tab->view.get());
    auto title = createString(adoptWK(WKPageCopyTitle(page)).get());
    if (!title.empty())
        tab->title = title;
    else
        tab->title = L"New Tab";

    if (!tab->activeUrl.empty()
        && (tab->activeUrl.rfind(L"http://", 0) == 0 || tab->activeUrl.rfind(L"https://", 0) == 0)
        && tab->activeUrl != tab->lastRecordedUrl) {
        tab->browser->recordVisit(tab->activeUrl, tab->title);
        tab->lastRecordedUrl = tab->activeUrl;
    }

    InvalidateRect(tab->browser->window, nullptr, TRUE);
    if (tab == tab->browser->active()) {
        std::wstring titleText = tab->title + L" — Aurora";
        SetWindowTextW(tab->browser->window, titleText.c_str());
    }
}

void didChangeActiveURL(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (!tab || !tab->browser)
        return;

    auto page = WKViewGetPage(tab->view.get());
    auto url = createString(adoptWK(WKPageCopyActiveURL(page)).get());

    if (tab->activeUrl != url) {
        if (tab->favicon) {
            DestroyIcon(tab->favicon);
            tab->favicon = nullptr;
        }
        tab->faviconRequestURL.clear();
    }
    tab->activeUrl = url;

    if (tab == tab->browser->active()) {
        if (url == L"about:blank" || url.empty())
            SetWindowTextW(tab->browser->addressBar, L"");
        else
            SetWindowTextW(tab->browser->addressBar, url.c_str());
    }

    if (url.empty() || url == L"about:blank")
        return;

    if ((url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0) && tab->faviconRequestURL != url) {
        tab->faviconRequestURL = url;
        requestFavicon(tab->browser, url);
    }

    if (url != tab->lastRecordedUrl && (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0)) {
        tab->browser->recordVisit(url, tab->title);
        tab->lastRecordedUrl = url;
    }

    InvalidateRect(tab->browser->window, nullptr, TRUE);
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

    case WM_CTLCOLOREDIT:
        if (state && reinterpret_cast<HWND>(lParam) == state->addressBar) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(45, 49, 52));
            SetBkColor(dc, RGB(240, 242, 243));
            static HBRUSH addressBrush = CreateSolidBrush(RGB(240, 242, 243));
            return reinterpret_cast<LRESULT>(addressBrush);
        }
        break;

    case WM_NCHITTEST: {
        POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(window, &point);

        RECT client { };
        GetClientRect(window, &client);

        int border = scaleForDpi(window, kResizeBorder);
        bool left = point.x < border;
        bool right = point.x >= client.right - border;
        bool top = point.y < border;
        bool bottom = point.y >= client.bottom - border;

        if (left && top) return HTTOPLEFT;
        if (right && top) return HTTOPRIGHT;
        if (left && bottom) return HTBOTTOMLEFT;
        if (right && bottom) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        int titleH = scaleForDpi(window, kTitleBarHeight);
        if (point.y < titleH) {
            // WM_NCHITTEST is queried during mouse movement/hover. It must
            // never execute a browser command. Only unused title-bar space
            // should become draggable.
            if (state && !state->topBarHasInteractiveHit(point))
                return HTCAPTION;
            return HTCLIENT;
        }

        return HTCLIENT;
    }

    case WM_LBUTTONDOWN: {
        if (!state)
            break;

        POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (state->handleTopBarHit(point))
            return 0;
        if (state->handleToolbarHit(point))
            return 0;
        if (state->handleSidebarHit(point))
            return 0;

        return DefWindowProcW(window, message, wParam, lParam);
    }

    case WM_AURORA_FAVICON_READY:
        if (state && lParam) {
            auto* result = reinterpret_cast<FaviconResult*>(lParam);
            for (auto& tab : state->tabs) {
                if (tab->activeUrl != result->url)
                    continue;
                if (tab->favicon)
                    DestroyIcon(tab->favicon);
                tab->favicon = result->icon;
                result->icon = nullptr;
                break;
            }
            if (result->icon)
                DestroyIcon(result->icon);
            delete result;
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;

    case WM_COMMAND:
        // Menu items and accelerators arrive with lParam == 0. Ignore
        // notifications emitted by child controls such as the address bar.
        if (state && lParam == 0)
            state->handleCommand(LOWORD(wParam));
        return 0;

    case WM_SIZE:
        if (state)
            state->resize();
        return 0;

    case WM_DPICHANGED:
        if (state) {
            for (auto& tab : state->tabs) {
                auto page = WKViewGetPage(tab->view.get());
                if (page)
                    WKPageSetCustomBackingScaleFactor(page, windowScaleFactor(window));
            }
            state->resize();
        }
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 780;
        info->ptMinTrackSize.y = 520;
        return 0;
    }

    case WM_DESTROY:
        if (state) {
            for (auto& tab : state->tabs) {
                if (tab->favicon) {
                    DestroyIcon(tab->favicon);
                    tab->favicon = nullptr;
                }
            }
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
        WS_EX_APPWINDOW,
        windowClassName,
        windowTitle,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
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

    // Prevent the Windows DWM accent color from appearing as a bright top border
    // around Aurora's custom light window chrome.
    if (HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll")) {
        using DwmSetWindowAttributeFunction = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFunction>(
            GetProcAddress(dwmapi, "DwmSetWindowAttribute")
        );
        if (setWindowAttribute) {
            DWORD borderColor = RGB(241, 242, 243);
            setWindowAttribute(window, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
        }
        FreeLibrary(dwmapi);
    }

    ShowWindow(window, showCommand == SW_MAXIMIZE ? SW_MAXIMIZE : SW_SHOWNORMAL);
    UpdateWindow(window);

    ACCEL accelerators[] = {
        { FCONTROL, 'L', kCommandAddress },
        { FCONTROL, 'R', kCommandReload },
        { FCONTROL, 'T', kCommandNewTab },
        { FCONTROL | FSHIFT, 'P', kMenuNewPrivateWindow },
        { FCONTROL, 'W', kCommandCloseTab },
        { FCONTROL | FSHIFT, 'Q', kMenuQuit },
        { FCONTROL, 'H', kMenuHistory },
        { FCONTROL | FSHIFT, 'B', kMenuBookmarks },
        { FCONTROL, 'D', kMenuAddBookmark },
        { FCONTROL | FSHIFT, 'D', kMenuReadingList },
        { FCONTROL, 'S', kMenuSavePage },
        { FCONTROL, 'P', kMenuPrint },
        { FCONTROL, 'F', kMenuFindInPage },
        { FALT, VK_LEFT, kCommandBack },
        { FALT, VK_RIGHT, kCommandForward }
    };

    HACCEL acceleratorTable = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));

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
