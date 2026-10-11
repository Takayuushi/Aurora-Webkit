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
#include <WebKit/WKHTTPCookieStoreRef.h>
#include <WebKit/WKData.h>
#include "WKPagePrivate.h"
#include <WebKit/WKFindOptions.h>
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
#include <charconv>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <cstring>
#include <sstream>
#include <string>
#include <ctime>
#include <thread>
#include <utility>
#include <vector>

#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <winreg.h>
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
constexpr UINT kCommandTabOverview = 1016;
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
constexpr UINT kMenuAddReadingList = 2034;
constexpr UINT kMenuOpenExtensionsFolder = 2035;
constexpr UINT kFindPrevious = 2101;
constexpr UINT kFindNext = 2102;
constexpr UINT kFindClose = 2103;
constexpr UINT kMenuThemeLight = 2030;
constexpr UINT kMenuThemeDark = 2031;
constexpr UINT kMenuThemeSystem = 2032;
constexpr UINT kMenuHomePage = 2033;
constexpr UINT kShareCopyLink = 2201;
constexpr UINT kShareEmailLink = 2202;
constexpr UINT kPrivacySiteInfo = 2210;
constexpr UINT kPrivacyShowHistory = 2211;
constexpr UINT kPrivacyClearHistory = 2212;
constexpr UINT kSettingsThemeCombo = 7101;
constexpr UINT kSettingsSearchCombo = 7102;
constexpr UINT kSettingsHomePageEdit = 7103;
constexpr UINT kSettingsDownloadsEdit = 7104;
constexpr UINT kSettingsBrowse = 7105;
constexpr UINT kSettingsSave = 7106;
constexpr UINT kSettingsCancel = 7107;
constexpr UINT kSettingsClearHistory = 7108;
constexpr UINT kSettingsClearWebsiteData = 7109;
constexpr UINT kSettingsJavaScript = 7110;
constexpr UINT kSettingsImages = 7111;
constexpr UINT kSettingsPopups = 7112;
constexpr UINT kSettingsLocalStorage = 7113;
constexpr UINT kSettingsDeveloperTools = 7114;
constexpr UINT WM_AURORA_WEBSITE_DATA_CLEARED = WM_APP + 21;

constexpr int kTitleBarHeight = 23;
constexpr int kToolbarHeight = 33;
constexpr int kToolbarHorizontalPadding = 12;
constexpr int kToolbarButtonSize = 24;
constexpr int kToolbarGap = 4;
constexpr int kLogoSize = 17;
constexpr int kTabMinWidth = 112;
constexpr int kTabMaxWidth = 280;
constexpr int kTrafficLightSize = 12;
constexpr int kTrafficLightGap = 8;
constexpr int kResizeBorder = 4;

struct BrowserState;
struct TabState;

int gLiveBrowserWindows = 0;
bool openNewBrowserWindow(BrowserState*, bool privateMode = false);

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
void openBrowserSettings(BrowserState*);
void savePageDataCompleted(WKDataRef, WKErrorRef, void*);
void printPageCountCompleted(WKRect*, uint32_t, double, WKErrorRef, void*);
void websiteDataCleared(void*);
void websiteDataStorageCleared(void*);
void websiteDataLocalStorageCleared(void*);
void websiteDataNetworkCacheCleared(void*);
void websiteDataMemoryCacheCleared(void*);
void websiteDataFetchCacheCleared(void*);
void printPDFDataCompleted(WKDataRef, WKErrorRef, void*);

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

struct ScriptExtension {
    std::wstring filename;
    std::wstring source;
};

struct ClearWebsiteDataContext {
    HWND owner { nullptr };
    WKWebsiteDataStoreRef store { nullptr };
    WKRetainPtr<WKHTTPCookieStoreRef> cookieStore;
};

struct SavePageRequest {
    HWND owner { nullptr };
    std::wstring path;
    WKPageRef page { nullptr };
    WKFrameRef frame { nullptr };
    WKPrintInfo printInfo { 1.0f, 595.0f, 842.0f };
};

struct DownloadEntry {
    std::wstring filename;
    std::wstring path;
    long long bytesWritten { 0 };
    long long totalBytes { 0 };
    bool finished { false };
    bool failed { false };
    WKDownloadRef download { nullptr };
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

bool parseInteger(const std::string& value, long long& output)
{
    if (value.empty())
        return false;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc { } && parsed.ptr == end;
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

std::wstring themePath()
{
    return applicationDataDirectory() + L"\\theme.txt";
}

std::wstring preferencesPath()
{
    return applicationDataDirectory() + L"\\preferences.tsv";
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

void setNativeWindowBorderColor(HWND window, bool dark)
{
    if (!window)
        return;
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi)
        return;
    using DwmSetWindowAttributeFunction = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFunction>(
        GetProcAddress(dwmapi, "DwmSetWindowAttribute")
    );
    if (setWindowAttribute) {
        DWORD borderColor = dark ? RGB(35, 35, 37) : RGB(241, 242, 243);
        setWindowAttribute(window, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
    }
    FreeLibrary(dwmapi);
}

void drawCompass(HDC dc, const RECT& rect)
{
    int size = std::min(rect.right - rect.left, rect.bottom - rect.top);
    int inset = std::max(1, size / 12);
    RECT circle { rect.left + inset, rect.top + inset, rect.left + size - inset, rect.top + size - inset };
    HPEN outline = CreatePen(PS_SOLID, std::max(1, size / 14), RGB(116, 126, 139));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, outline));
    HBRUSH background = CreateSolidBrush(RGB(250, 250, 252));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, background));
    Ellipse(dc, circle.left, circle.top, circle.right, circle.bottom);
    POINT needle[] = {
        { circle.left + size * 5 / 12, circle.top + size / 5 },
        { circle.left + size * 7 / 12, circle.top + size * 7 / 12 },
        { circle.left + size * 3 / 5, circle.top + size * 4 / 5 },
        { circle.left + size * 5 / 12, circle.top + size / 5 }
    };
    HBRUSH needleBrush = CreateSolidBrush(RGB(55, 116, 224));
    SelectObject(dc, needleBrush);
    Polygon(dc, needle, static_cast<int>(std::size(needle)));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(needleBrush);
    DeleteObject(background);
    DeleteObject(outline);
}

void drawChevron(HDC dc, const RECT& rect, bool forward, bool enabled, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 1, enabled ? (dark ? RGB(220, 220, 224) : RGB(55, 60, 63)) : (dark ? RGB(103, 103, 108) : RGB(171, 176, 179)));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;

    MoveToEx(dc, cx + (forward ? -4 : 4), cy - 7, nullptr);
    LineTo(dc, cx + (forward ? 4 : -4), cy);
    LineTo(dc, cx + (forward ? -4 : 4), cy + 7);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawReload(HDC dc, const RECT& rect, bool loading, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(220, 220, 224) : RGB(55, 60, 63));
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

void drawSidebar(HDC dc, const RECT& rect, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 2, dark ? RGB(220, 220, 224) : RGB(75, 80, 84));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    Rectangle(dc, rect.left + 9, rect.top + 9, rect.right - 9, rect.bottom - 9);
    int splitX = rect.left + (rect.right - rect.left) / 2;
    MoveToEx(dc, splitX, rect.top + 9, nullptr);
    LineTo(dc, splitX, rect.bottom - 9);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawShare(HDC dc, const RECT& rect, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 2, dark ? RGB(220, 220, 224) : RGB(75, 80, 84));
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

void drawTabOverview(HDC dc, const RECT& rect, bool dark)
{
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(205, 205, 210) : RGB(75, 80, 84));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));

    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;
    RoundRect(dc, cx - 7, cy - 5, cx + 7, cy + 7, 3, 3);
    RoundRect(dc, cx - 4, cy - 8, cx + 10, cy + 4, 3, 3);

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawDownload(HDC dc, const RECT& rect, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 2, dark ? RGB(220, 220, 224) : RGB(75, 80, 84));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    int x = (rect.left + rect.right) / 2;
    int shaftBottom = rect.bottom - 12;
    MoveToEx(dc, x, rect.top + 7, nullptr);
    LineTo(dc, x, shaftBottom);
    MoveToEx(dc, x - 6, shaftBottom - 6, nullptr);
    LineTo(dc, x, shaftBottom);
    LineTo(dc, x + 6, shaftBottom - 6);
    MoveToEx(dc, rect.left + 8, rect.bottom - 7, nullptr);
    LineTo(dc, rect.right - 8, rect.bottom - 7);

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawPageMenu(HDC dc, const RECT& rect, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(220, 220, 224) : RGB(55, 60, 63));
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

void drawMenu(HDC dc, const RECT& rect, bool dark = false)
{
    HBRUSH brush = CreateSolidBrush(dark ? RGB(220, 220, 224) : RGB(75, 80, 84));
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

void drawPlus(HDC dc, const RECT& rect, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(220, 220, 224) : RGB(70, 74, 77));
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

void drawClose(HDC dc, const RECT& rect, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(178, 178, 183) : RGB(105, 109, 112));
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

void drawShield(HDC dc, const RECT& rect, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(220, 220, 224) : RGB(67, 72, 75));
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

void drawReader(HDC dc, const RECT& rect, bool dark = false)
{
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(220, 220, 224) : RGB(67, 72, 75));
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
LRESULT CALLBACK findEditProcedure(HWND, UINT, WPARAM, LPARAM);

struct TabState {
    BrowserState* browser { nullptr };
    WKRetainPtr<WKViewRef> view;
    std::wstring title { L"New Tab" };
    std::wstring activeUrl;
    std::wstring internalPage { L"start" };
    bool hasInternalThemeSnapshot { false };
    bool internalDarkTheme { false };
    std::wstring lastRecordedUrl;
    std::wstring lastExtensionsUrl;
    std::wstring fallbackURL;
    HICON favicon { nullptr };
    std::wstring faviconRequestURL;
};

enum class ThemeMode { System, Light, Dark };

bool windowsPrefersDarkMode()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LONG result = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && value == 0;
}

struct BrowserState {
    HWND window { nullptr };
    HWND addressBar { nullptr };
    bool windowCounted { false };
    bool privateBrowsing { false };
    bool customMaximized { false };
    RECT restoredWindowRect { };
    WNDPROC addressBarOriginalProcedure { nullptr };
    WNDPROC findEditOriginalProcedure { nullptr };
    HWND findEdit { nullptr };
    HWND findPreviousButton { nullptr };
    HWND findNextButton { nullptr };
    HWND findCloseButton { nullptr };
    bool findBarVisible { false };
    HWND settingsWindow { nullptr };

    HFONT uiFont { nullptr };
    HBRUSH addressBarBrush { nullptr };
    HMENU menu { nullptr };
    HMENU bookmarksMenu { nullptr };
    bool sidebarOpen { false };
    int sidebarSection { 0 };
    ThemeMode themeMode { ThemeMode::System };
    std::wstring homePage { L"https://www.google.com" };
    std::wstring searchEngine { L"Google" };
    std::wstring downloadLocation;
    bool javascriptEnabled { true };
    bool imagesEnabled { true };
    bool allowPopups { false };
    bool localStorageEnabled { true };
    bool developerToolsEnabled { false };

    bool darkTheme() const { return themeMode == ThemeMode::Dark || (themeMode == ThemeMode::System && windowsPrefersDarkMode()); }

    void loadPreferences()
    {
        std::ifstream file(preferencesPath(), std::ios::binary);
        std::string line;
        while (std::getline(file, line)) {
            const size_t split = line.find('\t');
            if (split == std::string::npos)
                continue;
            const std::string key = line.substr(0, split);
            const std::wstring value = fromUTF8(line.substr(split + 1));
            if (key == "home")
                homePage = value;
            else if (key == "search" && (value == L"Google" || value == L"Bing" || value == L"DuckDuckGo"))
                searchEngine = value;
            else if (key == "downloads")
                downloadLocation = value;
            else if (key == "javascript")
                javascriptEnabled = value == L"1" || value == L"true";
            else if (key == "images")
                imagesEnabled = value == L"1" || value == L"true";
            else if (key == "popups")
                allowPopups = value == L"1" || value == L"true";
            else if (key == "localStorage")
                localStorageEnabled = value == L"1" || value == L"true";
            else if (key == "developerTools")
                developerToolsEnabled = value == L"1" || value == L"true";
        }
        if (homePage.empty())
            homePage = L"https://www.google.com";
        applyEnginePreferences();
    }

    void savePreferences()
    {
        std::ofstream file(preferencesPath(), std::ios::binary | std::ios::trunc);
        if (!file)
            return;
        std::wstring safeHome = homePage;
        std::wstring safeSearch = searchEngine;
        std::wstring safeDownloads = downloadLocation;
        replaceTabsAndNewlines(safeHome);
        replaceTabsAndNewlines(safeSearch);
        replaceTabsAndNewlines(safeDownloads);
        file << "home\t" << toUTF8(safeHome) << '\n'
             << "search\t" << toUTF8(safeSearch) << '\n'
             << "downloads\t" << toUTF8(safeDownloads) << '\n'
             << "javascript\t" << (javascriptEnabled ? "1" : "0") << '\n'
             << "images\t" << (imagesEnabled ? "1" : "0") << '\n'
             << "popups\t" << (allowPopups ? "1" : "0") << '\n'
             << "localStorage\t" << (localStorageEnabled ? "1" : "0") << '\n'
             << "developerTools\t" << (developerToolsEnabled ? "1" : "0") << '\n';
    }

    void applyEnginePreferences()
    {
        if (!preferences)
            return;
        WKPreferencesSetJavaScriptEnabled(preferences.get(), javascriptEnabled);
        WKPreferencesSetLoadsImagesAutomatically(preferences.get(), imagesEnabled);
        WKPreferencesSetJavaScriptCanOpenWindowsAutomatically(preferences.get(), allowPopups);
        WKPreferencesSetLocalStorageEnabled(preferences.get(), localStorageEnabled);
        WKPreferencesSetDeveloperExtrasEnabled(preferences.get(), developerToolsEnabled);
    }

    std::wstring effectiveDownloadsDirectory() const
    {
        return downloadLocation.empty() ? downloadsDirectory() : downloadLocation;
    }

    std::wstring searchURL(const std::wstring& query) const
    {
        const std::wstring encoded = percentEncode(query);
        if (searchEngine == L"Bing")
            return L"https://www.bing.com/search?q=" + encoded;
        if (searchEngine == L"DuckDuckGo")
            return L"https://duckduckgo.com/?q=" + encoded;
        return L"https://www.google.com/search?q=" + encoded;
    }

    void loadThemePreference()
    {
        std::wifstream file(themePath());
        std::wstring value;
        std::getline(file, value);
        if (value == L"light") themeMode = ThemeMode::Light;
        else if (value == L"dark") themeMode = ThemeMode::Dark;
        else themeMode = ThemeMode::System;
    }

    void saveThemePreference()
    {
        std::wofstream file(themePath(), std::ios::trunc);
        if (!file) return;
        file << (themeMode == ThemeMode::Light ? L"light" : themeMode == ThemeMode::Dark ? L"dark" : L"system") << L'\n';
    }

    void applyTheme()
    {
        saveThemePreference();
        setNativeWindowBorderColor(window, darkTheme());
        const size_t previousTab = activeTab;
        for (size_t i = 0; i < tabs.size(); ++i) {
            auto& tab = *tabs[i];
            if (tab.internalPage.empty())
                continue;
            activeTab = i;
            if (tab.internalPage == L"start") loadStartPage(tab);
            else if (tab.internalPage == L"bookmarks") loadBookmarksPage();
            else if (tab.internalPage == L"reading-list") loadReadingListPage();
            else if (tab.internalPage == L"history") loadHistoryPage();
            else if (tab.internalPage == L"extensions") loadExtensionsPage();
            else if (tab.internalPage == L"downloads") loadDownloadsPage();
        }
        if (!tabs.empty())
            activeTab = std::min(previousTab, tabs.size() - 1);
        if (auto* selected = active()) {
            const wchar_t* address = selected->activeUrl.empty() || selected->activeUrl == L"about:blank"
                ? L"" : selected->activeUrl.c_str();
            SetWindowTextW(addressBar, address);
            std::wstring titleText = selected->title + L" — Aurora";
            SetWindowTextW(window, titleText.c_str());
        }
        resize();
        InvalidateRect(window, nullptr, TRUE);
    }

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
    std::vector<ScriptExtension> extensionScripts;
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
        if (privateBrowsing)
            return;
        std::ifstream file(historyPath(), std::ios::binary);
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const size_t first = line.find('\t');
            const size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
            const size_t third = second == std::string::npos ? second : line.find('\t', second + 1);
            if (first == std::string::npos || second == std::string::npos || third == std::string::npos)
                continue;
            VisitEntry entry;
            const std::string visitsText = line.substr(0, first);
            const std::string timestampText = line.substr(first + 1, second - first - 1);
            if (!parseInteger(visitsText, entry.visits)
                || !parseInteger(timestampText, entry.lastVisited))
                continue;
            entry.url = fromUTF8(line.substr(second + 1, third - second - 1));
            entry.title = fromUTF8(line.substr(third + 1));
            if (!entry.url.empty() && entry.visits > 0)
                visits.push_back(std::move(entry));
        }
    }
    void saveVisitData()
    {
        std::ofstream file(historyPath(), std::ios::binary | std::ios::trunc);
        if (!file)
            return;
        for (const auto& entry : visits) {
            std::wstring url = entry.url;
            std::wstring title = entry.title;
            replaceTabsAndNewlines(url);
            replaceTabsAndNewlines(title);
            file << entry.visits << '\t' << entry.lastVisited << '\t'
                 << toUTF8(url) << '\t' << toUTF8(title) << '\n';
        }
    }
    void recordVisit(const std::wstring& url, const std::wstring& title)
    {
        if (privateBrowsing || url.empty())
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

    void updateVisitTitle(const std::wstring& url, const std::wstring& title)
    {
        if (privateBrowsing || url.empty() || title.empty())
            return;
        auto found = std::find_if(visits.begin(), visits.end(), [&](const VisitEntry& entry) {
            return entry.url == url;
        });
        if (found == visits.end()) {
            recordVisit(url, title);
            return;
        }
        if (found->title != title) {
            found->title = title;
            saveVisitData();
        }
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
        std::ifstream file(path, std::ios::binary);
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const size_t first = line.find('\t');
            const size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
            if (first == std::string::npos || second == std::string::npos)
                continue;
            long long added = 0;
            if (!parseInteger(line.substr(0, first), added))
                continue;
            std::wstring url = fromUTF8(line.substr(first + 1, second - first - 1));
            std::wstring title = fromUTF8(line.substr(second + 1));
            if (!url.empty())
                pages.push_back({ url, title.empty() ? url : title, added });
        }
    }
    void saveSavedPages(const std::wstring& path, const std::vector<SavedPage>& pages)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return;
        for (const auto& page : pages) {
            std::wstring url = page.url;
            std::wstring title = page.title;
            replaceTabsAndNewlines(url);
            replaceTabsAndNewlines(title);
            file << page.added << '\t' << toUTF8(url) << '\t' << toUTF8(title) << '\n';
        }
    }
    bool initialize(HWND parentWindow)
    {
        window = parentWindow;

        if (privateBrowsing) {
            websiteDataStore = adoptWK(WKWebsiteDataStoreCreateNonPersistentDataStore());
        } else {
            websiteDataStoreConfiguration = adoptWK(WKWebsiteDataStoreConfigurationCreate());
            if (!websiteDataStoreConfiguration)
                return false;
            websiteDataStore = adoptWK(WKWebsiteDataStoreCreateWithConfiguration(websiteDataStoreConfiguration.get()));
        }
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

        loadThemePreference();
        loadPreferences();
        loadVisitData();
        loadSavedPages(bookmarksPath(), bookmarks);
        loadSavedPages(readingListPath(), readingList);
        loadExtensionScripts();
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
        if (findEdit && findEditOriginalProcedure)
            SetWindowLongPtrW(findEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(findEditOriginalProcedure));
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
        HMENU readingListMenu = CreatePopupMenu();
        AppendMenuW(readingListMenu, MF_STRING, kMenuAddReadingList, L"Add to Reading List\tCtrl+Shift+D");
        AppendMenuW(readingListMenu, MF_STRING, kMenuReadingList, L"Show Reading List");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(readingListMenu), L"Reading List");

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
        AppendMenuW(menu, MF_STRING, kMenuHomePage, L"Home Page");
        HMENU appearanceMenu = CreatePopupMenu();
        AppendMenuW(appearanceMenu, MF_STRING, kMenuThemeLight, L"Light");
        AppendMenuW(appearanceMenu, MF_STRING, kMenuThemeDark, L"Dark");
        AppendMenuW(appearanceMenu, MF_STRING, kMenuThemeSystem, L"Automatic (System)");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(appearanceMenu), L"Appearance");
        HMENU extensionMenu = CreatePopupMenu();
        AppendMenuW(extensionMenu, MF_STRING, kMenuExtensions, L"Manage Extensions");
        AppendMenuW(extensionMenu, MF_STRING, kMenuOpenExtensionsFolder, L"Open Extensions Folder");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(extensionMenu), L"Extensions");
        AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
        AppendMenuW(menu, MF_STRING, kMenuAbout, L"About Aurora");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit Aurora\tCtrl+Shift+Q");
    }

    void loadExtensionScripts()
    {
        extensionScripts.clear();
        const std::wstring directory = extensionsPath();
        CreateDirectoryW(directory.c_str(), nullptr);
        const std::wstring pattern = directory + L"\\*.js";
        WIN32_FIND_DATAW entry { };
        HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE)
            return;

        do {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            const std::wstring path = directory + L"\\" + entry.cFileName;
            std::ifstream file(path, std::ios::binary);
            if (!file)
                continue;
            std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::wstring source = fromUTF8(bytes);
            if (!source.empty())
                extensionScripts.push_back({ entry.cFileName, std::move(source) });
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }

    void openExtensionsFolder()
    {
        const std::wstring directory = extensionsPath();
        CreateDirectoryW(directory.c_str(), nullptr);
        ShellExecuteW(window, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void runExtensionScripts(TabState* tab)
    {
        if (!tab || tab->browser != this || privateBrowsing || tab->activeUrl.empty())
            return;
        const bool webURL = tab->activeUrl.rfind(L"https://", 0) == 0 || tab->activeUrl.rfind(L"http://", 0) == 0;
        if (!webURL || tab->lastExtensionsUrl == tab->activeUrl)
            return;

        loadExtensionScripts();
        tab->lastExtensionsUrl = tab->activeUrl;
        for (const auto& extension : extensionScripts) {
            const std::string source = toUTF8(extension.source);
            if (source.empty())
                continue;
            auto script = adoptWK(WKStringCreateWithUTF8CString(source.c_str()));
            if (script)
                WKPageEvaluateJavaScriptInMainFrame(WKViewGetPage(tab->view.get()), script.get(), nullptr, nullptr);
        }
    }

    void loadExtensionsPage()
    {
        loadExtensionScripts();
        auto* tab = active();
        if (!tab)
            return;
        tab->internalPage = L"extensions";
        tab->hasInternalThemeSnapshot = true;
        tab->internalDarkTheme = darkTheme();
        const bool dark = darkTheme();
        std::string html = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Extensions</title><style>)HTML";
        html += dark ? R"CSS(:root{color-scheme:dark}body{margin:0;background:#1c1c1e;color:#f5f5f7;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif}.item{background:#2c2c2e;border:1px solid #48484a;padding:16px;border-radius:12px;margin:10px 0}.muted{color:#a1a1a6})CSS" : R"CSS(:root{color-scheme:light}body{margin:0;background:#f5f6f7;color:#202428;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif}.item{background:#fff;border:1px solid #e1e4e6;padding:16px;border-radius:12px;margin:10px 0}.muted{color:#73777d})CSS";
        html += R"HTML(</style></head><body><main style="max-width:900px;margin:auto;padding:48px 28px"><h1>Extensions</h1><p class="muted">Aurora supports local JavaScript user scripts. Put trusted .js files in the Extensions folder, then reload a page. Scripts run on regular HTTP/HTTPS pages and are disabled in Private Browsing.</p><p>)HTML";
        html += "Installed scripts: " + std::to_string(extensionScripts.size());
        html += "</p>";
        if (extensionScripts.empty())
            html += "<div class=\"item\">No scripts installed yet. Choose Extensions → Open Extensions Folder to add a .js file.</div>";
        else {
            for (const auto& extension : extensionScripts)
                html += "<div class=\"item\"><strong>" + toUTF8(htmlEscape(extension.filename)) + "</strong><p class=\"muted\">Enabled on regular web pages</p></div>";
        }
        html += "</main></body></html>";
        loadLocalHTML(*tab, html);
    }

    void loadStartPage(TabState& tab)
    {
        tab.internalPage = L"start";
        tab.hasInternalThemeSnapshot = true;
        tab.internalDarkTheme = darkTheme();
        auto frequent = frequentVisits(5);
        auto favorites = bookmarks;
        const bool dark = darkTheme();

        std::string html = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aurora</title>
<style>
:root{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif;color-scheme:)HTML" + std::string(dark ? "dark" : "light") + R"HTML(;color:)HTML" + (dark ? "#f5f5f7" : "#202124") + R"HTML(;--muted:)HTML" + (dark ? "#a1a1a6" : "#5f6368") + R"HTML(;--surface:)HTML" + (dark ? "#2c2c2e" : "#fff") + R"HTML(;--border:)HTML" + (dark ? "#48484a" : "#e2e4e7") + R"HTML(;--icon-bg:)HTML" + (dark ? "#3a3a3c" : "#f0f2f5") + R"HTML(;--icon-fg:)HTML" + (dark ? "#d1d1d6" : "#42474d") + R"HTML(;--hover:)HTML" + (dark ? "#38383a" : "#fafbfc") + R"HTML(}
*{box-sizing:border-box}
html,body{margin:0;min-height:100%;width:100%}
body{min-height:100vh;background:)HTML" + std::string(dark ? "#1c1c1e" : "#f5f5f7") + R"HTML(;color:)HTML" + (dark ? "#f5f5f7" : "#202124") + R"HTML(}
.page{min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:44px 24px}
.search{width:min(680px,90vw);height:46px;margin:0 auto 38px;padding:0 20px;border:1px solid #d8dadd;border-radius:14px;background:)HTML" + std::string(dark ? "#2c2c2e" : "#fff") + R"HTML(;color:)HTML" + (dark ? "#f5f5f7" : "#202124") + R"HTML(;outline:none;font-size:16px;box-shadow:0 2px 8px rgba(0,0,0,.05)}
.search:focus{border-color:#aeb8c7;box-shadow:0 0 0 3px rgba(65,120,220,.12)}
.search::placeholder{color:var(--muted)}
.section{width:min(820px,94vw);text-align:left;margin-bottom:14px;color:var(--muted);font-size:13px;font-weight:600}
.tiles{width:min(820px,94vw);display:flex;justify-content:flex-start;flex-wrap:wrap;gap:12px}
.tile{width:148px;min-height:106px;border-radius:14px;border:1px solid )HTML" + std::string(dark ? "#48484a" : "#e2e4e7") + R"HTML(;background:)HTML" + (dark ? "#2c2c2e" : "#fff") + R"HTML(;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px;color:)HTML" + (dark ? "#f5f5f7" : "#202124") + R"HTML(;cursor:pointer;transition:background .12s ease,border-color .12s ease}
.tile:hover{background:var(--hover);border-color:var(--border)}
.icon{width:36px;height:36px;border-radius:11px;display:grid;place-items:center;background:var(--icon-bg);color:var(--icon-fg);font-weight:600;font-size:16px}
.name{font-size:13px;max-width:128px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.host{font-size:11px;color:var(--muted);max-width:124px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.footer{margin-top:24px;color:#8a8f96;font-size:12px}
@media(max-width:700px){.tile{width:calc(50% - 6px)}.page{padding-top:28px}.tiles{justify-content:center}}
</style>
</head>
<body>
<div class="page">
<input id="search" class="search" autofocus placeholder="Search the web or enter an address">
<div class="section">)HTML";

        html += !favorites.empty() ? "Favorites" : (frequent.empty() ? "Quick Access" : "Frequently Visited");
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

        if (!favorites.empty()) {
            for (const auto& entry : favorites) {
                std::wstring host = entry.url;
                size_t start = host.find(L"://");
                if (start != std::wstring::npos)
                    host.erase(0, start + 3);
                size_t slash = host.find_first_of(L"/?#");
                if (slash != std::wstring::npos)
                    host.erase(slash);
                addTile(entry.title.empty() ? host : entry.title, entry.url, host);
            }
        } else if (frequent.empty()) {
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
                size_t slash = host.find_first_of(L"/?#");
                if (slash != std::wstring::npos)
                    host.erase(slash);
                addTile(entry.title.empty() ? host : entry.title, entry.url, host);
            }
        }

        html += R"HTML(
</div>
<div class="footer">Favorites and frequently visited websites are stored locally</div>
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
   location.href='__AURORA_SEARCH_PREFIX__'+encodeURIComponent(value);
});
</script>
</body></html>
)HTML";

        loadLocalHTML(tab, html);

        tab.title = L"New Tab";
        tab.activeUrl.clear();
        tab.lastRecordedUrl.clear();
        SetWindowTextW(addressBar, L"");
        SetWindowTextW(window, windowTitle);
    }

    void loadURL(TabState& tab, const std::wstring& value)
    {
        tab.internalPage.clear();
        tab.hasInternalThemeSnapshot = false;
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
            loadURL(*tab, searchURL(cleaned));

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
        if (select)
            activateTab(index);
        else
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

    void loadLocalHTML(TabState& tab, const std::string& sourceHtml)
    {
        std::string html = sourceHtml;
        const std::string searchPrefix = searchEngine == L"Bing"
            ? "https://www.bing.com/search?q="
            : (searchEngine == L"DuckDuckGo" ? "https://duckduckgo.com/?q=" : "https://www.google.com/search?q=");
        const std::string searchToken = "__AURORA_SEARCH_PREFIX__";
        const size_t searchPosition = html.find(searchToken);
        if (searchPosition != std::string::npos)
            html.replace(searchPosition, searchToken.size(), searchPrefix);

        auto htmlString = adoptWK(WKStringCreateWithUTF8CString(html.c_str()));
        auto baseURL = createWKURL(L"about:blank");
        WKPageLoadHTMLString(WKViewGetPage(tab.view.get()), htmlString.get(), baseURL.get());
    }

    std::string savedPagesHTML(const char* heading, const std::vector<SavedPage>& pages)
    {
        const bool dark = darkTheme();
        std::string html = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><title>Browser</title>
<style>
)HTML";
        html += dark ? R"CSS(
:root{color-scheme:dark}
body{margin:0;background:#1c1c1e;color:#f5f5f7;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif}
.wrap{max-width:980px;margin:auto;padding:54px 32px}
.item{padding:16px 18px;background:#2c2c2e;border:1px solid #48484a;border-radius:12px;margin:9px 0;display:block;color:inherit;text-decoration:none}
.name{font-size:15px}.url{font-size:12px;color:#a1a1a6;margin-top:5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.empty{padding:22px;color:#a1a1a6}
)CSS" : R"CSS(
:root{color-scheme:light}
body{margin:0;background:#f5f6f7;color:#202428;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif}
.wrap{max-width:980px;margin:auto;padding:54px 32px}
.item{padding:16px 18px;background:#fff;border:1px solid #e1e4e6;border-radius:12px;margin:9px 0;display:block;color:inherit;text-decoration:none}
.name{font-size:15px}.url{font-size:12px;color:#70777d;margin-top:5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.empty{padding:22px;color:#777}
)CSS";
        html += R"HTML(
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
        if (auto* tab = active()) {
            tab->internalPage = L"bookmarks";
            tab->hasInternalThemeSnapshot = true;
            tab->internalDarkTheme = darkTheme();
            loadLocalHTML(*tab, savedPagesHTML("Bookmarks", bookmarks));
        }
    }

    void loadReadingListPage()
    {
        if (auto* tab = active()) {
            tab->internalPage = L"reading-list";
            tab->hasInternalThemeSnapshot = true;
            tab->internalDarkTheme = darkTheme();
            loadLocalHTML(*tab, savedPagesHTML("Reading List", readingList));
        }
    }

    void loadDownloadsPage()
    {
        auto* tab = active();
        if (!tab)
            return;

        tab->internalPage = L"downloads";
        const bool dark = darkTheme();
        tab->internalPage = L"downloads";
        tab->hasInternalThemeSnapshot = true;
        tab->internalDarkTheme = dark;
        std::string html = "<!doctype html><html><head><meta charset='utf-8'><title>Downloads</title>"
            "<style>:root{color-scheme:" + std::string(dark ? "dark" : "light") + "}body{margin:0;background:"
            + (dark ? "#1c1c1e" : "#f5f6f7") + ";color:" + (dark ? "#f5f5f7" : "#202428")
            + ";font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Arial,sans-serif}"
            ".wrap{max-width:980px;margin:auto;padding:52px 32px}"
            "h1{font-size:30px;font-weight:500;margin:0 0 24px}"
            ".item{background:" + std::string(dark ? "#2c2c2e" : "#fff") + ";border:1px solid " + (dark ? "#48484a" : "#e1e4e6")
            + ";border-radius:12px;padding:16px 18px;margin:10px 0}"
            ".name{font-size:15px;font-weight:500}.path{font-size:12px;color:" + (dark ? "#a1a1a6" : "#73777d") + ";margin-top:5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
            ".meta{font-size:12px;color:" + (dark ? "#b8b8bd" : "#666") + ";margin-top:10px}.bar{height:6px;background:"
            + (dark ? "#48484a" : "#e2e5e7") + ";border-radius:3px;overflow:hidden;margin-top:8px}.fill{height:100%;background:#6b91c8}"
            ".empty{color:" + (dark ? "#a1a1a6" : "#777") + "}</style></head><body><main class='wrap'><h1>Downloads</h1>";

        if (downloads.empty()) {
            html += "<div class='empty'>No downloads yet.</div>";
        } else {
            for (const auto& item : downloads) {
                long long percent = 0;
                if (item.totalBytes > 0)
                    percent = std::clamp<long long>((item.bytesWritten * 100) / item.totalBytes, 0, 100);

                html += "<section class='item'><div class='name'>";
                html += toUTF8(htmlEscape(item.filename));
                html += "</div><div class='path'>";
                html += toUTF8(htmlEscape(item.path));
                html += "</div>";

                if (item.failed)
                    html += "<div class='meta'>Download failed.</div>";
                else if (item.finished)
                    html += "<div class='meta'>Downloaded.</div>";
                else {
                    html += "<div class='meta'>Downloading";
                    if (item.totalBytes > 0)
                        html += " — " + std::to_string(percent) + "%";
                    html += "</div><div class='bar'><div class='fill' style='width:";
                    html += std::to_string(percent);
                    html += "%'></div></div>";
                }

                html += "</section>";
            }
        }

        html += "</main></body></html>";
        loadLocalHTML(*tab, html);
        tab->activeUrl = L"about:downloads";
        tab->title = L"Downloads";
        InvalidateRect(window, nullptr, TRUE);
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
            size_t first = downloads.size() > 7 ? downloads.size() - 7 : 0;
            for (size_t i = downloads.size(); i-- > first;) {
                const auto& item = downloads[i];
                std::wstring label = item.failed ? L"Failed — " : (item.finished ? L"Downloaded — " : L"Downloading — ");
                label += item.filename;
                if (!item.finished && !item.failed && item.totalBytes > 0) {
                    long long percent = std::clamp<long long>((item.bytesWritten * 100) / item.totalBytes, 0, 100);
                    label += L"  " + std::to_wstring(percent) + L"%";
                }
                AppendMenuW(downloadsMenu, MF_STRING, 6000 + static_cast<UINT>(i), label.c_str());
            }
        }

        AppendMenuW(downloadsMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(downloadsMenu, MF_STRING, 5996, L"Show All Downloads");

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
        tab->internalPage = L"history";

        const bool dark = darkTheme();
        tab->internalPage = L"history";
        tab->hasInternalThemeSnapshot = true;
        tab->internalDarkTheme = dark;
        std::string html = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><title>History</title>
<style>
)HTML";
        html += dark ? R"CSS(
:root{color-scheme:dark}
body{margin:0;background:#1c1c1e;color:#f5f5f7;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif}
.item{padding:15px 18px;background:#2c2c2e;border:1px solid #48484a;border-radius:12px;margin:8px 0}
.name{font-size:15px}.meta{font-size:12px;color:#a1a1a6;margin-top:5px}
.item a{color:inherit;text-decoration:none}.empty{padding:22px;color:#a1a1a6}
)CSS" : R"CSS(
:root{color-scheme:light}
body{margin:0;background:#f5f6f7;color:#202428;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif}
.item{padding:15px 18px;background:#fff;border:1px solid #e1e4e6;border-radius:12px;margin:8px 0}
.name{font-size:15px}.meta{font-size:12px;color:#73777d;margin-top:5px}
.item a{color:inherit;text-decoration:none}.empty{padding:22px;color:#777}
)CSS";
        html += R"HTML(
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
        if (!tab || tab->activeUrl.empty() || tab->activeUrl == L"about:blank" || tab->activeUrl.rfind(L"about:", 0) == 0) {
            MessageBoxW(window, L"Open a webpage before adding it to Favorites.", L"Bookmarks", MB_OK | MB_ICONINFORMATION);
            return;
        }

        auto found = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const SavedPage& page) {
            return page.url == tab->activeUrl;
        });

        const bool removed = found != bookmarks.end();
        if (removed)
            bookmarks.erase(found);
        else
            bookmarks.push_back({ tab->activeUrl, tab->title.empty() ? tab->activeUrl : tab->title, currentUnixTime() });

        saveSavedPages(bookmarksPath(), bookmarks);
        InvalidateRect(window, nullptr, TRUE);
        MessageBoxW(window, removed ? L"Removed from Favorites." : L"Added to Favorites.", L"Bookmarks", MB_OK | MB_ICONINFORMATION);
    }

    void addReadingList()
    {
        auto* tab = active();
        if (!tab || tab->activeUrl.empty() || tab->activeUrl == L"about:blank" || tab->activeUrl.rfind(L"about:", 0) == 0) {
            MessageBoxW(window, L"Open a webpage before adding it to Reading List.", L"Reading List", MB_OK | MB_ICONINFORMATION);
            return;
        }

        auto found = std::find_if(readingList.begin(), readingList.end(), [&](const SavedPage& page) {
            return page.url == tab->activeUrl;
        });

        if (found != readingList.end()) {
            MessageBoxW(window, L"This page is already in Reading List.", L"Reading List", MB_OK | MB_ICONINFORMATION);
            return;
        }

        readingList.push_back({ tab->activeUrl, tab->title.empty() ? tab->activeUrl : tab->title, currentUnixTime() });
        saveSavedPages(readingListPath(), readingList);
        InvalidateRect(window, nullptr, TRUE);
        MessageBoxW(window, L"Added to Reading List.", L"Reading List", MB_OK | MB_ICONINFORMATION);
    }

    void clearHistory()
    {
        visits.clear();
        if (!privateBrowsing)
            saveVisitData();
        loadHistoryPage();
    }

    void savePageAs()
    {
        auto* tab = active();
        if (!tab || !tab->activeUrl.size() || tab->activeUrl == L"about:blank" || tab->activeUrl.rfind(L"about:", 0) == 0) {
            MessageBoxW(window, L"Open a webpage before saving it.", L"Save Page As", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring filename = tab->title.empty() ? L"Webpage" : tab->title;
        for (auto& ch : filename) {
            if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
                ch = L'_';
        }
        filename += L".mhtml";

        std::vector<wchar_t> path(32768, L'\0');
        wcsncpy_s(path.data(), path.size(), filename.c_str(), _TRUNCATE);
        const wchar_t filter[] = L"Web archive (*.mhtml)\0*.mhtml\0All files (*.*)\0*.*\0\0";
        OPENFILENAMEW dialog { };
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = path.data();
        dialog.nMaxFile = static_cast<DWORD>(path.size());
        dialog.lpstrDefExt = L"mhtml";
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog))
            return;

        auto* request = new SavePageRequest;
        request->owner = window;
        request->path = path.data();
        request->page = WKViewGetPage(tab->view.get());
        request->frame = WKPageGetMainFrame(request->page);
        WKPageGetContentsAsMHTMLData(request->page, request, savePageDataCompleted);
    }

    void printPage()
    {
        auto* tab = active();
        if (!tab || !tab->activeUrl.size() || tab->activeUrl == L"about:blank" || tab->activeUrl.rfind(L"about:", 0) == 0) {
            MessageBoxW(window, L"Open a webpage before printing.", L"Print", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring filename = tab->title.empty() ? L"Webpage" : tab->title;
        for (auto& ch : filename) {
            if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
                ch = L'_';
        }
        filename += L".pdf";

        std::vector<wchar_t> path(32768, L'\0');
        wcsncpy_s(path.data(), path.size(), filename.c_str(), _TRUNCATE);
        const wchar_t filter[] = L"PDF document (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0\0";
        OPENFILENAMEW dialog { };
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = path.data();
        dialog.nMaxFile = static_cast<DWORD>(path.size());
        dialog.lpstrDefExt = L"pdf";
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog))
            return;

        auto* request = new SavePageRequest;
        request->owner = window;
        request->path = path.data();
        request->page = WKViewGetPage(tab->view.get());
        request->frame = WKPageGetMainFrame(request->page);
        request->printInfo = { 1.0f, 595.0f, 842.0f };
        WKPageComputePagesForPrinting(request->page, request->frame, request->printInfo, printPageCountCompleted, request);
    }

    void showShareMenu()
    {
        HMENU shareMenu = CreatePopupMenu();
        if (!shareMenu)
            return;
        AppendMenuW(shareMenu, MF_STRING, kShareCopyLink, L"Copy Link");
        AppendMenuW(shareMenu, MF_STRING, kShareEmailLink, L"Email Link");
        AppendMenuW(shareMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(shareMenu, MF_STRING, kMenuAddBookmark, L"Add Bookmark");
        RECT client { };
        GetClientRect(window, &client);
        POINT point { client.right - scaleForDpi(window, 190), titleBarHeight() + toolbarHeight() };
        ClientToScreen(window, &point);
        TrackPopupMenu(shareMenu, TPM_RIGHTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
        DestroyMenu(shareMenu);
    }

    void copyActiveUrl()
    {
        auto* tab = active();
        if (!tab || tab->activeUrl.empty())
            return;
        if (!OpenClipboard(window))
            return;
        EmptyClipboard();
        const SIZE_T bytes = (tab->activeUrl.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            void* target = GlobalLock(memory);
            if (target) {
                memcpy(target, tab->activeUrl.c_str(), bytes);
                GlobalUnlock(memory);
                if (!SetClipboardData(CF_UNICODETEXT, memory))
                    GlobalFree(memory);
            } else {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
    }

    void showPrivacyMenu()
    {
        HMENU privacyMenu = CreatePopupMenu();
        if (!privacyMenu)
            return;
        AppendMenuW(privacyMenu, MF_STRING, kPrivacySiteInfo, L"Connection Information");
        AppendMenuW(privacyMenu, MF_STRING, kPrivacyShowHistory, L"Show History");
        AppendMenuW(privacyMenu, MF_STRING, kPrivacyClearHistory, L"Clear History");
        RECT client { };
        GetClientRect(window, &client);
        POINT point { scaleForDpi(window, 50), titleBarHeight() + toolbarHeight() };
        ClientToScreen(window, &point);
        TrackPopupMenu(privacyMenu, TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
        DestroyMenu(privacyMenu);
    }

    void showTabOverviewMenu()
    {
        if (tabs.empty())
            return;
        HMENU overview = CreatePopupMenu();
        if (!overview)
            return;

        for (size_t i = 0; i < tabs.size(); ++i) {
            std::wstring label = tabs[i]->title.empty() ? L"New Tab" : tabs[i]->title;
            if (label.size() > 72) {
                label.resize(69);
                label += L"...";
            }
            UINT flags = MF_STRING;
            if (i == activeTab)
                flags |= MF_CHECKED;
            AppendMenuW(overview, flags, 3000 + static_cast<UINT>(i), label.c_str());
        }

        int pad = scaleForDpi(window, kToolbarHorizontalPadding);
        int button = scaleForDpi(window, kToolbarButtonSize);
        int gap = scaleForDpi(window, kToolbarGap);
        POINT point { GetClientWidth(window) - pad - button - gap - button / 2,
            titleBarHeight() + toolbarHeight() };
        ClientToScreen(window, &point);
        TrackPopupMenu(overview, TPM_RIGHTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
        DestroyMenu(overview);
    }

    void showMenu()
    {
        if (bookmarksMenu) {
            auto* tab = active();
            bool bookmarked = tab && !tab->activeUrl.empty()
                && std::any_of(bookmarks.begin(), bookmarks.end(), [&](const SavedPage& page) {
                    return page.url == tab->activeUrl;
                });

            ModifyMenuW(
                bookmarksMenu,
                kMenuAddBookmark,
                MF_BYCOMMAND | MF_STRING,
                kMenuAddBookmark,
                bookmarked ? L"Remove Bookmark	Ctrl+D" : L"Bookmark This Page	Ctrl+D"
            );
            CheckMenuItem(
                bookmarksMenu,
                kMenuAddBookmark,
                MF_BYCOMMAND | (bookmarked ? MF_CHECKED : MF_UNCHECKED)
            );
        }

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
        int rightControls = button * 4 + gap * 5;

        int left = pad + leftControls + scaleForDpi(window, 12);
        int right = client.right - pad - rightControls;

        return {
            left,
            titleBarHeight() + scaleForDpi(window, 7),
            std::max(left + scaleForDpi(window, 220), right),
            titleBarHeight() + toolbarHeight() - scaleForDpi(window, 7)
        };
    }

    void resize()
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
                FALSE
            );
        }

        if (findBarVisible)
            positionFindBar();
        InvalidateRect(window, nullptr, FALSE);
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
            showPrivacyMenu();
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
            showShareMenu();
            break;
        case kShareCopyLink:
            copyActiveUrl();
            break;
        case kShareEmailLink:
            if (auto* tab = active()) {
                std::wstring mailto = L"mailto:?body=" + percentEncode(tab->activeUrl);
                ShellExecuteW(window, L"open", mailto.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        case kPrivacySiteInfo:
            if (auto* tab = active()) {
                const bool secure = tab->activeUrl.rfind(L"https://", 0) == 0;
                const wchar_t* prefix = secure ? L"Secure connection (HTTPS).\n\n" : L"This page is not using HTTPS.\n\n";
                MessageBoxW(window, (std::wstring(prefix) + tab->activeUrl).c_str(), L"Connection Information", MB_OK | (secure ? MB_ICONINFORMATION : MB_ICONWARNING));
            }
            break;
        case kPrivacyShowHistory:
            if (addTab(true)) loadHistoryPage();
            break;
        case kPrivacyClearHistory:
            clearHistory();
            break;
        case kCommandDownloads:
            showDownloadsMenu();
            break;
        case kCommandTabOverview:
            showTabOverviewMenu();
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
            openNewBrowserWindow(this);
            break;
        case kMenuNewPrivateWindow:
            openNewBrowserWindow(this, true);
            break;
        case kMenuHistory:
            if (addTab(true))
                loadHistoryPage();
            break;
        case kMenuClearHistory:
            clearHistory();
            break;
        case kMenuAddBookmark:
            toggleBookmark();
            break;
        case kMenuBookmarks:
            if (addTab(true))
                loadBookmarksPage();
            break;
        case kMenuAddReadingList:
            addReadingList();
            break;
        case kMenuReadingList:
            if (addTab(true))
                loadReadingListPage();
            break;
        case kMenuDownloads:
            if (addTab(true))
                loadDownloadsPage();
            break;
        case 5996:
            if (addTab(true))
                loadDownloadsPage();
            break;
        case kMenuSavePage:
            savePageAs();
            break;
        case kMenuPrint:
            printPage();
            break;
        case kMenuFindInPage:
            showFindBar();
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
        case kMenuThemeLight:
            themeMode = ThemeMode::Light; applyTheme(); break;
        case kMenuThemeDark:
            themeMode = ThemeMode::Dark; applyTheme(); break;
        case kMenuThemeSystem:
            themeMode = ThemeMode::System; applyTheme(); break;
        case kMenuExtensions:
            if (addTab(true))
                loadExtensionsPage();
            break;
        case kMenuOpenExtensionsFolder:
            openExtensionsFolder();
            break;
        case kMenuStartPage:
            if (auto* tab = active())
                loadStartPage(*tab);
            break;
        case kMenuHomePage:
            if (auto* tab = active())
                loadURL(*tab, homePage);
            break;
        case kMenuSettings:
            openBrowserSettings(this);
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

    void positionFindBar()
    {
        if (!window || !findEdit)
            return;
        RECT client { };
        GetClientRect(window, &client);
        const int clientWidth = static_cast<int>(client.right);
        const int width = std::min(scaleForDpi(window, 430), std::max(scaleForDpi(window, 320), clientWidth - scaleForDpi(window, 20)));
        const int height = scaleForDpi(window, 32);
        const int left = std::max(scaleForDpi(window, 10), clientWidth - width - scaleForDpi(window, 16));
        const int top = titleBarHeight() + toolbarHeight() + scaleForDpi(window, 8);
        MoveWindow(findEdit, left + scaleForDpi(window, 10), top + scaleForDpi(window, 5), width - scaleForDpi(window, 164), height - scaleForDpi(window, 10), TRUE);
        MoveWindow(findPreviousButton, left + width - scaleForDpi(window, 150), top + scaleForDpi(window, 4), scaleForDpi(window, 42), height - scaleForDpi(window, 8), TRUE);
        MoveWindow(findNextButton, left + width - scaleForDpi(window, 104), top + scaleForDpi(window, 4), scaleForDpi(window, 42), height - scaleForDpi(window, 8), TRUE);
        MoveWindow(findCloseButton, left + width - scaleForDpi(window, 56), top + scaleForDpi(window, 4), scaleForDpi(window, 46), height - scaleForDpi(window, 8), TRUE);
    }

    void showFindBar()
    {
        if (!findEdit) {
            findEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP | ES_LEFT,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandAddress + 30)),
                GetModuleHandleW(nullptr), nullptr);
            findPreviousButton = CreateWindowExW(0, L"BUTTON", L"Previous",
                WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindPrevious)),
                GetModuleHandleW(nullptr), nullptr);
            findNextButton = CreateWindowExW(0, L"BUTTON", L"Next",
                WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindNext)),
                GetModuleHandleW(nullptr), nullptr);
            findCloseButton = CreateWindowExW(0, L"BUTTON", L"×",
                WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindClose)),
                GetModuleHandleW(nullptr), nullptr);
            if (!findEdit || !findPreviousButton || !findNextButton || !findCloseButton) {
                if (findEdit) DestroyWindow(findEdit);
                if (findPreviousButton) DestroyWindow(findPreviousButton);
                if (findNextButton) DestroyWindow(findNextButton);
                if (findCloseButton) DestroyWindow(findCloseButton);
                findEdit = findPreviousButton = findNextButton = findCloseButton = nullptr;
                return;
            }
            for (HWND control : { findEdit, findPreviousButton, findNextButton, findCloseButton })
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
            SetWindowLongPtrW(findEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
            findEditOriginalProcedure = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(findEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(findEditProcedure)));
        }

        findBarVisible = true;
        positionFindBar();
        for (HWND control : { findEdit, findPreviousButton, findNextButton, findCloseButton }) {
            ShowWindow(control, SW_SHOW);
            SetWindowPos(control, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
        SetFocus(findEdit);
        SendMessageW(findEdit, EM_SETSEL, 0, -1);
        InvalidateRect(window, nullptr, TRUE);
    }

    void hideFindBar()
    {
        if (auto* tab = active()) {
            if (auto page = WKViewGetPage(tab->view.get()))
                WKPageHideFindUI(page);
            SetFocus(WKViewGetWindow(tab->view.get()));
        }
        findBarVisible = false;
        for (HWND control : { findEdit, findPreviousButton, findNextButton, findCloseButton })
            if (control)
                ShowWindow(control, SW_HIDE);
        InvalidateRect(window, nullptr, TRUE);
    }

    void searchInPage(bool backwards)
    {
        if (!findEdit || !findBarVisible)
            return;
        const int length = GetWindowTextLengthW(findEdit);
        if (length <= 0) {
            if (auto* tab = active())
                WKPageHideFindUI(WKViewGetPage(tab->view.get()));
            return;
        }
        std::wstring query(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(findEdit, query.data(), length + 1);
        query.resize(static_cast<size_t>(length));
        const std::string utf8 = toUTF8(query);
        auto searchString = adoptWK(WKStringCreateWithUTF8CString(utf8.c_str()));
        unsigned options = kWKFindOptionsCaseInsensitive | kWKFindOptionsWrapAround
            | kWKFindOptionsShowFindIndicator | kWKFindOptionsShowHighlight;
        if (backwards)
            options |= kWKFindOptionsBackwards;
        if (auto* tab = active())
            WKPageFindString(WKViewGetPage(tab->view.get()), searchString.get(), static_cast<WKFindOptions>(options), 1000);
    }

    bool handleFindControlCommand(UINT command, HWND control, UINT notification)
    {
        if (control == findEdit && notification == EN_CHANGE) {
            searchInPage(false);
            return true;
        }
        if (command == kFindPrevious && control == findPreviousButton) {
            searchInPage(true);
            return true;
        }
        if (command == kFindNext && control == findNextButton) {
            searchInPage(false);
            return true;
        }
        if (command == kFindClose && control == findCloseButton) {
            hideFindBar();
            return true;
        }
        return false;
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
        RECT overviewRect { downloadsRect.right + gap, titleH, downloadsRect.right + gap + button, titleH + toolbarH };
        RECT menuRect { overviewRect.right + gap, titleH, overviewRect.right + gap + button, titleH + toolbarH };

        if (PtInRect(&shareRect, point)) {
            handleCommand(kCommandShare);
            return true;
        }
        if (PtInRect(&downloadsRect, point)) {
            handleCommand(kCommandDownloads);
            return true;
        }
        if (PtInRect(&overviewRect, point)) {
            handleCommand(kCommandTabOverview);
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

        const bool dark = darkTheme();
        int width = scaleForDpi(window, 286);
        int top = titleBarHeight() + toolbarHeight();
        RECT client { };
        GetClientRect(window, &client);

        RECT panel { 0, top, width, client.bottom };
        HBRUSH brush = CreateSolidBrush(dark ? RGB(35, 35, 37) : RGB(247, 248, 249));
        FillRect(dc, &panel, brush);
        DeleteObject(brush);

        HPEN divider = CreatePen(PS_SOLID, 1, dark ? RGB(62, 62, 65) : RGB(220, 223, 225));
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
        SetTextColor(dc, dark ? RGB(240, 240, 242) : RGB(35, 39, 42));
        const wchar_t* headingText = sidebarSection == 0 ? L"Bookmarks" : (sidebarSection == 1 ? L"Reading List" : L"History");
        DrawTextW(dc, headingText, -1, &heading, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT closeRect { width - scaleForDpi(window, 48), top + scaleForDpi(window, 10),
            width - scaleForDpi(window, 10), top + scaleForDpi(window, 44) };
        drawClose(dc, closeRect, dark);

        RECT selector { scaleForDpi(window, 12), top + scaleForDpi(window, 52),
            width - scaleForDpi(window, 12), top + scaleForDpi(window, 84) };
        int part = (selector.right - selector.left) / 3;
        for (int i = 0; i < 3; ++i) {
            RECT r { selector.left + part * i, selector.top, selector.left + part * (i + 1), selector.bottom };
            if (i == sidebarSection) {
                HBRUSH selected = CreateSolidBrush(dark ? RGB(73, 73, 77) : RGB(229, 232, 235));
                FillRect(dc, &r, selected);
                DeleteObject(selected);
            }
            SetTextColor(dc, i == sidebarSection ? (dark ? RGB(245, 245, 247) : RGB(35, 39, 42)) : (dark ? RGB(160, 160, 165) : RGB(115, 120, 124)));
            const wchar_t* label = i == 0 ? L"Bookmarks" : (i == 1 ? L"Reading List" : L"History");
            DrawTextW(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        int rowTop = top + scaleForDpi(window, 100);
        int rowHeight = scaleForDpi(window, 62);
        size_t count = sidebarSection == 0 ? bookmarks.size() : (sidebarSection == 1 ? readingList.size() : visits.size());

        if (!count) {
            RECT empty { scaleForDpi(window, 18), rowTop, width - scaleForDpi(window, 18), rowTop + scaleForDpi(window, 42) };
            SetTextColor(dc, dark ? RGB(160, 160, 165) : RGB(125, 130, 134));
            const wchar_t* emptyText = sidebarSection == 0 ? L"No bookmarks yet." : (sidebarSection == 1 ? L"No saved pages yet." : L"No browsing history.");
            DrawTextW(dc, emptyText, -1, &empty, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        } else {
            SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
            if (sidebarSection == 0) {
                for (size_t i = 0; i < std::min<size_t>(10, bookmarks.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 6), width - scaleForDpi(window, 16), y + scaleForDpi(window, 28) };
                    DrawTextW(dc, bookmarks[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 31), width - scaleForDpi(window, 16), y + scaleForDpi(window, 50) };
                    SetTextColor(dc, dark ? RGB(161, 161, 166) : RGB(126, 131, 135));
                    DrawTextW(dc, bookmarks[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
                }
            } else if (sidebarSection == 1) {
                for (size_t i = 0; i < std::min<size_t>(10, readingList.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 6), width - scaleForDpi(window, 16), y + scaleForDpi(window, 28) };
                    DrawTextW(dc, readingList[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 31), width - scaleForDpi(window, 16), y + scaleForDpi(window, 50) };
                    SetTextColor(dc, dark ? RGB(161, 161, 166) : RGB(126, 131, 135));
                    DrawTextW(dc, readingList[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
                }
            } else {
                auto sorted = visits;
                std::sort(sorted.begin(), sorted.end(), [](const VisitEntry& a, const VisitEntry& b) { return a.lastVisited > b.lastVisited; });
                for (size_t i = 0; i < std::min<size_t>(10, sorted.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 6), width - scaleForDpi(window, 16), y + scaleForDpi(window, 28) };
                    DrawTextW(dc, sorted[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 31), width - scaleForDpi(window, 16), y + scaleForDpi(window, 50) };
                    SetTextColor(dc, dark ? RGB(161, 161, 166) : RGB(126, 131, 135));
                    DrawTextW(dc, sorted[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
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

        const bool dark = darkTheme();
        HBRUSH pageBrush = CreateSolidBrush(dark ? RGB(28, 28, 30) : RGB(249, 249, 250));
        FillRect(dc, &client, pageBrush);
        DeleteObject(pageBrush);

        HBRUSH titleBrush = CreateSolidBrush(dark ? RGB(35, 35, 37) : RGB(241, 242, 243));
        RECT titleRect { 0, 0, client.right, titleH };
        FillRect(dc, &titleRect, titleBrush);
        DeleteObject(titleBrush);

        HBRUSH toolbarBrush = CreateSolidBrush(dark ? RGB(43, 43, 45) : RGB(246, 247, 248));
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

            HBRUSH tabBrush = CreateSolidBrush(dark ? (i == activeTab ? RGB(66, 66, 69) : RGB(47, 47, 49)) : (i == activeTab ? RGB(250, 251, 251) : RGB(239, 240, 241)));
            HPEN tabPen = CreatePen(PS_SOLID, 1, dark ? (i == activeTab ? RGB(91, 91, 95) : RGB(55, 55, 58)) : (i == activeTab ? RGB(211, 214, 216) : RGB(229, 231, 232)));
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
                drawCompass(dc, iconRect);

            RECT titleText = tabRect;
            titleText.left = iconRect.right + MulDiv(6, dpi, 96);
            titleText.right -= MulDiv(31, dpi, 96);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, dark ? RGB(240, 240, 242) : RGB(45, 49, 52));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dc, uiFont));
            DrawTextW(dc, tabs[i]->title.c_str(), -1, &titleText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, oldFont);

            RECT closeRect {
                tabRect.right - MulDiv(26, dpi, 96),
                tabRect.top,
                tabRect.right - MulDiv(2, dpi, 96),
                tabRect.bottom
            };
            drawClose(dc, closeRect, dark);
        }

        RECT plusRect { plusLeft, 0, plusLeft + button, titleH };
        drawPlus(dc, plusRect, dark);

        int x = pad;
        RECT backRect { x, titleH, x + button, titleH + toolbarH };
        drawChevron(dc, backRect, false, active() && WKPageCanGoBack(WKViewGetPage(active()->view.get())), dark);
        x += button + gap;
        RECT forwardRect { x, titleH, x + button, titleH + toolbarH };
        drawChevron(dc, forwardRect, true, active() && WKPageCanGoForward(WKViewGetPage(active()->view.get())), dark);
        x += button + gap;
        RECT sidebarRect { x, titleH, x + button, titleH + toolbarH };
        drawSidebar(dc, sidebarRect, dark);

        RECT pill = addressPillRect();
        HBRUSH pillBrush = CreateSolidBrush(dark ? RGB(57, 57, 60) : RGB(245, 246, 247));
        HPEN pillPen = CreatePen(PS_SOLID, 1, dark ? RGB(77, 77, 81) : RGB(208, 212, 214));
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
        drawShield(dc, privacyGlyph, dark);
        bool loading = active() && WKPageGetEstimatedProgress(WKViewGetPage(active()->view.get())) < 1.0;
        RECT reloadRect { pill.right - MulDiv(30, dpi, 96), pill.top, pill.right, pill.bottom };
        drawReload(dc, reloadRect, loading, dark);

        int rightStart = pill.right + gap * 2;
        RECT shareRect { rightStart, titleH, rightStart + button, titleH + toolbarH };
        RECT downloadsRect { shareRect.right + gap, titleH, shareRect.right + gap + button, titleH + toolbarH };
        RECT overviewRect { downloadsRect.right + gap, titleH, downloadsRect.right + gap + button, titleH + toolbarH };
        RECT menuRect { overviewRect.right + gap, titleH, overviewRect.right + gap + button, titleH + toolbarH };

        drawShare(dc, shareRect, dark);
        drawDownload(dc, downloadsRect, dark);
        drawTabOverview(dc, overviewRect, dark);
        drawMenu(dc, menuRect, dark);
    }
};

std::vector<std::unique_ptr<BrowserState>> gAdditionalBrowserWindows;

bool openNewBrowserWindow(BrowserState* source, bool privateMode)
{
    auto browser = std::make_unique<BrowserState>();
    browser->privateBrowsing = privateMode;
    if (source) {
        browser->themeMode = source->themeMode;
        browser->homePage = source->homePage;
        browser->searchEngine = source->searchEngine;
        browser->downloadLocation = source->downloadLocation;
    }
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
        GetModuleHandleW(nullptr),
        browser.get()
    );
    if (!window)
        return false;

    gAdditionalBrowserWindows.push_back(std::move(browser));
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    return true;
}

struct SettingsDialogState {
    BrowserState* browser { nullptr };
    HWND themeCombo { nullptr };
    HWND searchCombo { nullptr };
    HWND homeEdit { nullptr };
    HWND downloadsEdit { nullptr };
    HWND javascriptCheck { nullptr };
    HWND imagesCheck { nullptr };
    HWND popupsCheck { nullptr };
    HWND localStorageCheck { nullptr };
    HWND developerToolsCheck { nullptr };
};

LRESULT CALLBACK settingsWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* dialog = reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* createInfo = reinterpret_cast<CREATESTRUCTW*>(lParam);
        dialog = static_cast<SettingsDialogState*>(createInfo->lpCreateParams);
        if (!dialog)
            return FALSE;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }

    switch (message) {
    case WM_CREATE: {
        if (!dialog || !dialog->browser)
            return -1;
        BrowserState* browser = dialog->browser;
        HFONT font = browser->uiFont;
        auto label = [&](const wchar_t* text, int x, int y, int width = 170) {
            HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                x, y, width, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return control;
        };
        auto combo = [&](int id, int x, int y, int width) {
            HWND control = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                x, y, width, 180, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return control;
        };
        auto edit = [&](int id, int x, int y, int width, const std::wstring& value) {
            HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                x, y, width, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return control;
        };

        label(L"Appearance", 30, 32);
        dialog->themeCombo = combo(kSettingsThemeCombo, 220, 30, 360);
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic (follow Windows)"));
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Light"));
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dark"));
        SendMessageW(dialog->themeCombo, CB_SETCURSEL,
            browser->themeMode == ThemeMode::System ? 0 : (browser->themeMode == ThemeMode::Light ? 1 : 2), 0);

        label(L"Default search engine", 30, 88);
        dialog->searchCombo = combo(kSettingsSearchCombo, 220, 86, 360);
        SendMessageW(dialog->searchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Google"));
        SendMessageW(dialog->searchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Bing"));
        SendMessageW(dialog->searchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DuckDuckGo"));
        int searchIndex = browser->searchEngine == L"Bing" ? 1 : (browser->searchEngine == L"DuckDuckGo" ? 2 : 0);
        SendMessageW(dialog->searchCombo, CB_SETCURSEL, searchIndex, 0);

        label(L"Home page", 30, 144);
        dialog->homeEdit = edit(kSettingsHomePageEdit, 220, 142, 360, browser->homePage);
        label(L"Downloads folder", 30, 200);
        const std::wstring downloads = browser->effectiveDownloadsDirectory();
        dialog->downloadsEdit = edit(kSettingsDownloadsEdit, 220, 198, 270, downloads);
        HWND browse = CreateWindowExW(0, L"BUTTON", L"Choose…",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            500, 198, 80, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsBrowse)),
            GetModuleHandleW(nullptr), nullptr);
        if (font) SendMessageW(browse, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        label(L"Website settings", 30, 248);
        auto check = [&](const wchar_t* text, int id, int x, int y, bool checked) {
            HWND control = CreateWindowExW(0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                x, y, 180, 24, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
            return control;
        };
        dialog->javascriptCheck = check(L"Enable JavaScript", kSettingsJavaScript, 220, 244, browser->javascriptEnabled);
        dialog->imagesCheck = check(L"Load images automatically", kSettingsImages, 220, 270, browser->imagesEnabled);
        dialog->popupsCheck = check(L"Allow pop-up windows", kSettingsPopups, 410, 244, browser->allowPopups);
        dialog->localStorageCheck = check(L"Allow website storage", kSettingsLocalStorage, 410, 270, browser->localStorageEnabled);
        dialog->developerToolsCheck = check(L"Developer tools", kSettingsDeveloperTools, 220, 296, browser->developerToolsEnabled);

        label(L"Privacy", 30, 336);
        label(L"Clear local browsing history or website data.", 220, 336, 390);
        HWND clearHistory = CreateWindowExW(0, L"BUTTON", L"Clear History",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            220, 364, 130, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsClearHistory)),
            GetModuleHandleW(nullptr), nullptr);
        HWND clearWebsiteData = CreateWindowExW(0, L"BUTTON", L"Clear Website Data",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            360, 364, 150, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsClearWebsiteData)),
            GetModuleHandleW(nullptr), nullptr);
        if (font) {
            SendMessageW(clearHistory, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(clearWebsiteData, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }

        HWND save = CreateWindowExW(0, L"BUTTON", L"Save",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            400, 408, 85, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsSave)),
            GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            495, 408, 85, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsCancel)),
            GetModuleHandleW(nullptr), nullptr);
        if (font) {
            SendMessageW(save, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        return 0;
    }

    case WM_COMMAND:
        if (!dialog || !dialog->browser)
            return 0;
        if (LOWORD(wParam) == kSettingsBrowse) {
            BROWSEINFOW info { };
            info.hwndOwner = window;
            info.lpszTitle = L"Choose the folder for downloads";
            info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE folder = SHBrowseForFolderW(&info);
            if (folder) {
                wchar_t path[MAX_PATH] { };
                if (SHGetPathFromIDListW(folder, path))
                    SetWindowTextW(dialog->downloadsEdit, path);
                CoTaskMemFree(folder);
            }
            return 0;
        }
        if (LOWORD(wParam) == kSettingsClearHistory) {
            if (MessageBoxW(window, L"Clear the saved browsing history?", L"Clear History", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            dialog->browser->visits.clear();
            if (!dialog->browser->privateBrowsing)
                dialog->browser->saveVisitData();
            dialog->browser->applyTheme();
            return 0;
        }
        if (LOWORD(wParam) == kSettingsClearWebsiteData) {
            if (MessageBoxW(window, L"Clear cookies, cached site data, and local website storage for this browser window?", L"Clear Website Data", MB_YESNO | MB_ICONWARNING) != IDYES)
                return 0;
            auto* context = new ClearWebsiteDataContext {
                dialog->browser->window,
                dialog->browser->websiteDataStore.get()
            };
            WKWebsiteDataStoreClearStorage(context->store, context, websiteDataStorageCleared);
            return 0;
        }
        if (LOWORD(wParam) == kSettingsSave) {
            BrowserState* browser = dialog->browser;
            const LRESULT theme = SendMessageW(dialog->themeCombo, CB_GETCURSEL, 0, 0);
            browser->themeMode = theme == 1 ? ThemeMode::Light : (theme == 2 ? ThemeMode::Dark : ThemeMode::System);
            const LRESULT search = SendMessageW(dialog->searchCombo, CB_GETCURSEL, 0, 0);
            browser->searchEngine = search == 1 ? L"Bing" : (search == 2 ? L"DuckDuckGo" : L"Google");

            int homeLength = GetWindowTextLengthW(dialog->homeEdit);
            std::wstring home(static_cast<size_t>(homeLength) + 1, L'\0');
            GetWindowTextW(dialog->homeEdit, home.data(), homeLength + 1);
            home.resize(static_cast<size_t>(homeLength));
            while (!home.empty() && iswspace(home.front())) home.erase(home.begin());
            while (!home.empty() && iswspace(home.back())) home.pop_back();
            if (home.empty())
                home = L"https://www.google.com";
            else if (home.find(L"://") == std::wstring::npos && home.rfind(L"about:", 0) != 0)
                home = L"https://" + home;
            browser->homePage = home;

            int folderLength = GetWindowTextLengthW(dialog->downloadsEdit);
            std::wstring folder(static_cast<size_t>(folderLength) + 1, L'\0');
            GetWindowTextW(dialog->downloadsEdit, folder.data(), folderLength + 1);
            folder.resize(static_cast<size_t>(folderLength));
            while (!folder.empty() && iswspace(folder.front())) folder.erase(folder.begin());
            while (!folder.empty() && iswspace(folder.back())) folder.pop_back();
            if (!folder.empty()) {
                DWORD attributes = GetFileAttributesW(folder.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    MessageBoxW(window, L"That downloads folder does not exist. Choose an existing folder or leave it blank to use the Windows Downloads folder.", L"Settings", MB_OK | MB_ICONWARNING);
                    return 0;
                }
            }
            browser->downloadLocation = folder;
            browser->javascriptEnabled = SendMessageW(dialog->javascriptCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            browser->imagesEnabled = SendMessageW(dialog->imagesCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            browser->allowPopups = SendMessageW(dialog->popupsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            browser->localStorageEnabled = SendMessageW(dialog->localStorageCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            browser->developerToolsEnabled = SendMessageW(dialog->developerToolsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            browser->savePreferences();
            browser->applyEnginePreferences();
            browser->applyTheme();
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == kSettingsCancel) {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        if (dialog && dialog->browser && dialog->browser->darkTheme()) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(240, 240, 242));
            SetBkColor(dc, RGB(38, 38, 40));
            static HBRUSH darkBrush = CreateSolidBrush(RGB(38, 38, 40));
            return reinterpret_cast<LRESULT>(darkBrush);
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        if (dialog && dialog->browser) {
            HWND owner = dialog->browser->window;
            dialog->browser->settingsWindow = nullptr;
            EnableWindow(owner, TRUE);
            SetForegroundWindow(owner);
        }
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        delete dialog;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void openBrowserSettings(BrowserState* browser)
{
    if (!browser || !browser->window)
        return;
    if (browser->settingsWindow) {
        SetForegroundWindow(browser->settingsWindow);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW cls { };
        cls.cbSize = sizeof(cls);
        cls.lpfnWndProc = settingsWindowProcedure;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        cls.lpszClassName = L"AuroraSettingsWindow";
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        classRegistered = true;
    }

    auto* dialog = new SettingsDialogState { browser };
    HWND settings = CreateWindowExW(WS_EX_DLGMODALFRAME,
        L"AuroraSettingsWindow", L"Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 500,
        browser->window, nullptr, GetModuleHandleW(nullptr), dialog);
    if (!settings) {
        delete dialog;
        return;
    }
    browser->settingsWindow = settings;
    EnableWindow(browser->window, FALSE);
    RECT ownerRect { };
    GetWindowRect(browser->window, &ownerRect);
    SetWindowPos(settings, HWND_TOP,
        ownerRect.left + ((ownerRect.right - ownerRect.left) - 640) / 2,
        ownerRect.top + ((ownerRect.bottom - ownerRect.top) - 500) / 2,
        640, 500, SWP_SHOWWINDOW);
    UpdateWindow(settings);
}

static bool writeDataToFile(const std::wstring& path, WKDataRef data)
{
    if (!data)
        return false;
    const unsigned char* bytes = WKDataGetBytes(data);
    size_t remaining = WKDataGetSize(data);
    if (!bytes || !remaining)
        return false;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    bool ok = true;
    while (remaining) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(file, bytes, chunk, &written, nullptr) || !written) {
            ok = false;
            break;
        }
        bytes += written;
        remaining -= written;
    }
    CloseHandle(file);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}

void savePageDataCompleted(WKDataRef data, WKErrorRef error, void* context)
{
    std::unique_ptr<SavePageRequest> request(static_cast<SavePageRequest*>(context));
    if (!request)
        return;

    if (error || !writeDataToFile(request->path, data)) {
        if (IsWindow(request->owner))
            MessageBoxW(request->owner, L"WebKit could not create the web archive. Try again after the page finishes loading.", L"Save Page As", MB_OK | MB_ICONWARNING);
        return;
    }

    if (IsWindow(request->owner))
        MessageBoxW(request->owner, (L"Page saved to:\n" + request->path).c_str(), L"Save Page As", MB_OK | MB_ICONINFORMATION);
}

void printPDFDataCompleted(WKDataRef data, WKErrorRef error, void* context)
{
    std::unique_ptr<SavePageRequest> request(static_cast<SavePageRequest*>(context));
    if (!request)
        return;

    WKPageEndPrinting(request->page);
    if (error || !writeDataToFile(request->path, data)) {
        if (IsWindow(request->owner))
            MessageBoxW(request->owner, L"WebKit could not create a PDF for this page.", L"Print", MB_OK | MB_ICONWARNING);
        return;
    }

    // Use the registered Windows PDF handler's print verb as the platform equivalent
    // of Safari's print panel. The saved PDF remains available at the chosen path.
    HINSTANCE result = ShellExecuteW(request->owner, L"print", request->path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32 && IsWindow(request->owner))
        MessageBoxW(request->owner, L"The page was saved as a PDF, but Windows has no PDF print handler configured. Open the PDF and print it from your PDF reader.", L"Print", MB_OK | MB_ICONINFORMATION);
}

void printPageCountCompleted(WKRect*, uint32_t pageCount, double, WKErrorRef error, void* context)
{
    auto* request = static_cast<SavePageRequest*>(context);
    if (!request)
        return;

    if (error || pageCount == 0) {
        if (IsWindow(request->owner))
            MessageBoxW(request->owner, L"WebKit could not prepare this page for printing.", L"Print", MB_OK | MB_ICONWARNING);
        delete request;
        return;
    }

    WKPageBeginPrinting(request->page, request->frame, request->printInfo);
    WKPageDrawPagesToPDF(request->page, request->frame, request->printInfo, 0, pageCount, printPDFDataCompleted, request);
}

void websiteDataCleared(void* context)
{
    std::unique_ptr<ClearWebsiteDataContext> request(static_cast<ClearWebsiteDataContext*>(context));
    if (request && IsWindow(request->owner))
        PostMessageW(request->owner, WM_AURORA_WEBSITE_DATA_CLEARED, 0, 0);
}

void websiteDataStorageCleared(void* context)
{
    auto* request = static_cast<ClearWebsiteDataContext*>(context);
    WKWebsiteDataStoreRemoveLocalStorage(request->store, context, websiteDataLocalStorageCleared);
}

void websiteDataLocalStorageCleared(void* context)
{
    auto* request = static_cast<ClearWebsiteDataContext*>(context);
    WKWebsiteDataStoreRemoveNetworkCache(request->store, context, websiteDataNetworkCacheCleared);
}

void websiteDataNetworkCacheCleared(void* context)
{
    auto* request = static_cast<ClearWebsiteDataContext*>(context);
    WKWebsiteDataStoreRemoveMemoryCaches(request->store, context, websiteDataMemoryCacheCleared);
}

void websiteDataMemoryCacheCleared(void* context)
{
    auto* request = static_cast<ClearWebsiteDataContext*>(context);
    WKWebsiteDataStoreRemoveAllFetchCaches(request->store, context, websiteDataFetchCacheCleared);
}

void websiteDataFetchCacheCleared(void* context)
{
    auto* request = static_cast<ClearWebsiteDataContext*>(context);
    request->cookieStore = adoptWK(WKWebsiteDataStoreGetHTTPCookieStore(request->store));
    if (!request->cookieStore) {
        websiteDataCleared(context);
        return;
    }
    WKHTTPCookieStoreDeleteAllCookies(request->cookieStore.get(), context, websiteDataCleared);
}

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

LRESULT CALLBACK findEditProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* browser = reinterpret_cast<BrowserState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (browser && message == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            browser->searchInPage((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            browser->hideFindBar();
            return 0;
        }
    }
    if (browser && browser->findEditOriginalProcedure)
        return CallWindowProcW(browser->findEditOriginalProcedure, window, message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
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

WKStringRef downloadDecideDestinationWithResponse(WKDownloadRef download, WKURLResponseRef response, WKStringRef suggestedFilename, const void* clientInfo)
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

    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    std::wstring folder = browser ? browser->effectiveDownloadsDirectory() : downloadsDirectory();
    if (folder.empty())
        return nullptr;

    std::wstring path = folder + L"\\" + filename;
    if (browser) {
        browser->downloads.push_back({ filename, path, 0, 0, false, false, download });
        if (browser->downloads.size() > 32)
            browser->downloads.erase(browser->downloads.begin());
        InvalidateRect(browser->window, nullptr, TRUE);
    }

    std::string utf8 = toUTF8(path);
    return WKStringCreateWithUTF8CString(utf8.c_str());
}

void downloadDidWriteData(WKDownloadRef download, long long, long long totalBytesWritten, long long totalBytesExpectedToWrite, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return;

    for (auto it = browser->downloads.rbegin(); it != browser->downloads.rend(); ++it) {
        if (it->download == download) {
            it->bytesWritten = totalBytesWritten;
            it->totalBytes = totalBytesExpectedToWrite;
            InvalidateRect(browser->window, nullptr, TRUE);
            return;
        }
    }
}

void downloadDidFinish(WKDownloadRef download, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return;

    for (auto it = browser->downloads.rbegin(); it != browser->downloads.rend(); ++it) {
        if (it->download == download) {
            it->finished = true;
            break;
        }
    }

    InvalidateRect(browser->window, nullptr, TRUE);
}

void downloadDidFailWithError(WKDownloadRef download, WKErrorRef error, WKDataRef, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return;

    for (auto it = browser->downloads.rbegin(); it != browser->downloads.rend(); ++it) {
        if (it->download == download) {
            it->failed = true;
            break;
        }
    }

    InvalidateRect(browser->window, nullptr, TRUE);

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
    client.didWriteData = downloadDidWriteData;
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
    if (!tab || !tab->browser)
        return;

    auto page = WKViewGetPage(tab->view.get());
    if (page && WKPageGetEstimatedProgress(page) < 1.0)
        tab->lastExtensionsUrl.clear();
    if (page && WKPageGetEstimatedProgress(page) >= 1.0) {
        tab->browser->runExtensionScripts(tab);
        auto url = createString(adoptWK(WKPageCopyActiveURL(page)).get());
        if (!url.empty()
            && (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0)
            && url != tab->lastRecordedUrl) {
            tab->activeUrl = url;
            tab->browser->recordVisit(url, tab->title.empty() ? url : tab->title);
            tab->lastRecordedUrl = url;
        }
    }

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
        && (tab->activeUrl.rfind(L"http://", 0) == 0 || tab->activeUrl.rfind(L"https://", 0) == 0))
        tab->browser->updateVisitTitle(tab->activeUrl, tab->title);

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
    if (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0) {
        tab->internalPage.clear();
        tab->hasInternalThemeSnapshot = false;
    }

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

    InvalidateRect(tab->browser->window, nullptr, FALSE);
}

void didChangeEstimatedProgress(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        InvalidateRect(tab->browser->window, nullptr, FALSE);}

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
        if (!state->windowCounted) {
            state->windowCounted = true;
            ++gLiveBrowserWindows;
        }
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
        if (state && (reinterpret_cast<HWND>(lParam) == state->addressBar || reinterpret_cast<HWND>(lParam) == state->findEdit)) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, state->darkTheme() ? RGB(240, 240, 242) : RGB(45, 49, 52));
            SetBkColor(dc, state->darkTheme() ? RGB(57, 57, 60) : RGB(240, 242, 243));
            static HBRUSH lightAddressBrush = CreateSolidBrush(RGB(240, 242, 243));
            static HBRUSH darkAddressBrush = CreateSolidBrush(RGB(57, 57, 60));
            return reinterpret_cast<LRESULT>(state->darkTheme() ? darkAddressBrush : lightAddressBrush);
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

    case WM_AURORA_WEBSITE_DATA_CLEARED:
        if (state)
            MessageBoxW(window, L"Website data has been cleared.", L"Privacy", MB_OK | MB_ICONINFORMATION);
        return 0;

    case WM_AURORA_FAVICON_READY:
        if (state && lParam) {
            auto* result = reinterpret_cast<FaviconResult*>(lParam);
            for (auto& tab : state->tabs) {
                if (tab->activeUrl != result->url || tab->faviconRequestURL != result->url)
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
        if (state && lParam != 0 && state->handleFindControlCommand(
                LOWORD(wParam), reinterpret_cast<HWND>(lParam), HIWORD(wParam)))
            return 0;
        // Menu items and accelerators arrive with lParam == 0.
        if (state && lParam == 0)
            state->handleCommand(LOWORD(wParam));
        return 0;

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        if (state && state->themeMode == ThemeMode::System)
            state->applyTheme();
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
            if (state->windowCounted) {
                state->windowCounted = false;
                --gLiveBrowserWindows;
            }
        }
        if (gLiveBrowserWindows <= 0)
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

    // Match the DWM border to the selected browser appearance.
    setNativeWindowBorderColor(window, browserState.darkTheme());

    ShowWindow(window, showCommand == SW_MAXIMIZE ? SW_MAXIMIZE : SW_SHOWNORMAL);
    UpdateWindow(window);

    ACCEL accelerators[] = {
        { FCONTROL, 'L', kCommandAddress },
        { FCONTROL, 'R', kCommandReload },
        { FCONTROL, 'T', kCommandNewTab },
        { FCONTROL, 'N', kMenuNewWindow },
        { FCONTROL, 'I', kMenuDownloads },
        { FCONTROL, 'J', kMenuDownloads },
        { FCONTROL, VK_OEM_COMMA, kMenuSettings },
        { FCONTROL | FSHIFT, 'P', kMenuNewPrivateWindow },
        { FCONTROL, 'W', kCommandCloseTab },
        { FCONTROL | FSHIFT, 'Q', kMenuQuit },
        { FCONTROL, 'H', kMenuHistory },
        { FCONTROL | FSHIFT, 'B', kMenuBookmarks },
        { FCONTROL, 'D', kMenuAddBookmark },
        { FCONTROL | FSHIFT, 'D', kMenuAddReadingList },
        { FCONTROL, 'S', kMenuSavePage },
        { FCONTROL, 'P', kMenuPrint },
        { FCONTROL, 'F', kMenuFindInPage },
        { FALT, VK_LEFT, kCommandBack },
        { FALT, VK_RIGHT, kCommandForward }
    };

    HACCEL acceleratorTable = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));

    MSG message { };
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        bool acceleratorHandled = TranslateAcceleratorW(window, acceleratorTable, &message) != 0;
        if (!acceleratorHandled) {
            for (const auto& browser : gAdditionalBrowserWindows) {
                if (browser && browser->window
                    && TranslateAcceleratorW(browser->window, acceleratorTable, &message)) {
                    acceleratorHandled = true;
                    break;
                }
            }
        }
        if (!acceleratorHandled) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (acceleratorTable)
        DestroyAcceleratorTable(acceleratorTable);

    return static_cast<int>(message.wParam);
}
