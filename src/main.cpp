#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

#include <WebKit/WKContext.h>
#include <WebKit/WKDownloadClient.h>
#include <WebKit/WKDownloadRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKNavigationActionRef.h>
#include <WebKit/WKFramePolicyListener.h>
#include <WebKit/WKFrameInfoRef.h>
#include <WebKit/WKURLRequest.h>
#include <WebKit/WKNavigationResponseRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKURLResponse.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageFullScreenClient.h>
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
#include <system_error>
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
constexpr UINT kEditUndo = 2220;
constexpr UINT kEditRedo = 2221;
constexpr UINT kEditCut = 2222;
constexpr UINT kEditCopy = 2223;
constexpr UINT kEditPaste = 2224;
constexpr UINT kEditSelectAll = 2225;
constexpr UINT kSidebarBookmarks = 2230;
constexpr UINT kSidebarReadingList = 2231;
constexpr UINT kSidebarHistory = 2232;
constexpr UINT WM_AURORA_FAVICON_READY = WM_APP + 20;
constexpr UINT_PTR kDownloadsAnimationTimerId = 0xA112;
constexpr UINT_PTR kChromeAnimationTimerId = 0xA113;

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
constexpr UINT kPrivacyWebsiteSettings = 2213;
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
constexpr UINT kSettingsNavBase = 7120;
constexpr int kSettingsPageCount = 6;
constexpr UINT WM_AURORA_WEBSITE_DATA_CLEARED = WM_APP + 21;

constexpr int kTitleBarHeight = 0;
constexpr int kToolbarHeight = 36;
constexpr int kTabsBarHeight = 28;
constexpr int kToolbarHorizontalPadding = 18;
constexpr int kToolbarButtonSize = 20;
constexpr int kToolbarGap = 4;
constexpr int kTabMinWidth = 112;
constexpr int kTrafficLightSize = 13;
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
void decidePolicyForNavigationResponse(WKPageRef, WKNavigationResponseRef, WKFramePolicyListenerRef, WKTypeRef, const void*);
void decidePolicyForNavigationAction(WKPageRef, WKNavigationActionRef, WKFramePolicyListenerRef, WKTypeRef, const void*);
void navigationActionDidBecomeDownload(WKPageRef, WKNavigationActionRef, WKDownloadRef, const void*);
void navigationResponseDidBecomeDownload(WKPageRef, WKNavigationResponseRef, WKDownloadRef, const void*);
void contextMenuDidCreateDownload(WKPageRef, WKDownloadRef, const void*);
void openBrowserSettings(BrowserState*, int requestedSection = 0);
void savePageDataCompleted(WKDataRef, WKErrorRef, void*);
void websiteDataCleared(void*);
void websiteDataStorageCleared(void*);
void websiteDataLocalStorageCleared(void*);
void websiteDataNetworkCacheCleared(void*);
void websiteDataMemoryCacheCleared(void*);
void browserWillEnterFullScreen(WKPageRef, WKCompletionListenerRef, const void*);
void browserBeganEnterFullScreen(WKPageRef, WKRect, WKRect, const void*);
void browserExitFullScreen(WKPageRef, const void*);
void browserBeganExitFullScreen(WKPageRef, WKRect, WKRect, WKCompletionListenerRef, const void*);
void websiteDataFetchCacheCleared(void*);

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
};

struct DownloadEntry {
    std::wstring filename;
    std::wstring path;
    long long bytesWritten { 0 };
    long long totalBytes { 0 };
    bool finished { false };
    bool failed { false };
    WKDownloadRef download { nullptr };
    bool cancelled { false };
    double displayedProgressPercent { 0.0 };
};

std::string formatDownloadBytes(long long bytes)
{
    const unsigned long long value = bytes > 0 ? static_cast<unsigned long long>(bytes) : 0ULL;
    if (value < 1024ULL)
        return std::to_string(value) + " B";
    if (value < 1024ULL * 1024ULL)
        return std::to_string(value / 1024ULL) + " KB";
    if (value < 1024ULL * 1024ULL * 1024ULL)
        return std::to_string(value / (1024ULL * 1024ULL)) + " MB";
    return std::to_string(value / (1024ULL * 1024ULL * 1024ULL)) + " GB";
}

std::wstring createString(WKStringRef string)
{
    if (!string)
        return { };

    size_t maximumSize = WKStringGetMaximumUTF8CStringSize(string);
    if (!maximumSize)
        return { };

    std::string utf8(maximumSize, '\0');
    const size_t copiedSize = WKStringGetUTF8CString(string, utf8.data(), maximumSize);
    if (copiedSize == 0 || copiedSize > maximumSize)
        return { };
    // WKStringGetUTF8CString returns the byte count including its final NUL.
    // Trim to that count before converting; otherwise the zero-filled buffer tail
    // is mistaken for filename characters and sanitized into trailing underscores.
    utf8.resize(copiedSize - 1);

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
        // Keep Windows from drawing an accent-coloured outline around Aurora.
        // The custom chrome already draws its own edge and remains resizable.
        DWORD borderColor = 0xFFFFFFFE; // DWMWA_COLOR_NONE
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

Gdiplus::Color iconGdiColor(COLORREF color)
{
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

void addRoundedIconRectangle(Gdiplus::GraphicsPath& path, float left, float top,
    float width, float height, float radius)
{
    radius = std::min(radius, std::min(width, height) / 2.0f);
    const float diameter = std::max(0.5f, radius * 2.0f);
    path.StartFigure();
    path.AddArc(left, top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(left + width - diameter, top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(left + width - diameter, top + height - diameter,
        diameter, diameter, 0.0f, 90.0f);
    path.AddArc(left, top + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void drawRoundedControl(HDC dc, const RECT& rect, COLORREF fillColor,
    COLORREF borderColor, float radius, float borderWidth = 1.0f)
{
    const float width = static_cast<float>(rect.right - rect.left);
    const float height = static_cast<float>(rect.bottom - rect.top);
    if (width <= 0.0f || height <= 0.0f)
        return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::GraphicsPath path;
    addRoundedIconRectangle(path, static_cast<float>(rect.left) + 0.5f,
        static_cast<float>(rect.top) + 0.5f, width - 1.0f, height - 1.0f, radius);
    Gdiplus::SolidBrush fill(iconGdiColor(fillColor));
    Gdiplus::Pen border(iconGdiColor(borderColor), borderWidth);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&border, &path);
}

COLORREF blendColor(COLORREF from, COLORREF to, float amount)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    const auto blend = [amount](BYTE a, BYTE b) -> BYTE {
        return static_cast<BYTE>(static_cast<float>(a)
            + (static_cast<float>(b) - static_cast<float>(a)) * amount + 0.5f);
    };
    return RGB(blend(GetRValue(from), GetRValue(to)),
        blend(GetGValue(from), GetGValue(to)),
        blend(GetBValue(from), GetBValue(to)));
}

void drawChevron(HDC dc, const RECT& rect, bool forward, bool enabled, bool dark = false)
{
    const COLORREF color = enabled
        ? (dark ? RGB(222, 222, 226) : RGB(62, 66, 70))
        : (dark ? RGB(105, 105, 110) : RGB(173, 176, 180));
    const float scale = std::max(0.7f, static_cast<float>(rect.bottom - rect.top) / 28.0f);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float direction = forward ? 1.0f : -1.0f;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(iconGdiColor(color), 1.25f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::PointF points[] = {
        { cx - direction * 3.3f * scale, cy - 5.4f * scale },
        { cx + direction * 2.5f * scale, cy },
        { cx - direction * 3.3f * scale, cy + 5.4f * scale }
    };
    graphics.DrawLines(&pen, points, 3);
}

void drawReload(HDC dc, const RECT& rect, bool loading, bool dark = false)
{
    const COLORREF color = dark ? RGB(224, 224, 228) : RGB(69, 73, 77);
    const float scale = std::max(0.7f,
        static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) / 28.0f);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(iconGdiColor(color), 1.3f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    if (loading) {
        // Safari's reload control becomes a stop glyph while navigation is active.
        const float half = 3.4f * scale;
        Gdiplus::GraphicsPath stop;
        addRoundedIconRectangle(stop, cx - half, cy - half, half * 2.0f, half * 2.0f, 0.8f * scale);
        graphics.DrawPath(&pen, &stop);
        return;
    }

    const float radius = 7.0f * scale;
    graphics.DrawArc(&pen,
        Gdiplus::RectF(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f),
        45.0f, 270.0f);

    const float tipX = cx + radius * 0.70710678f;
    const float tipY = cy - radius * 0.70710678f;
    // Two short, clean strokes meet at the arc's endpoint to form the arrowhead.
    graphics.DrawLine(&pen, tipX - 3.5f * scale, tipY - 0.25f * scale, tipX, tipY);
    graphics.DrawLine(&pen, tipX, tipY, tipX - 0.25f * scale, tipY + 3.5f * scale);
}

void drawSidebar(HDC dc, const RECT& rect, bool dark = false)
{
    const COLORREF color = dark ? RGB(225, 225, 229) : RGB(63, 67, 71);
    const float scale = std::max(0.7f, static_cast<float>(rect.bottom - rect.top) / 26.0f);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float left = cx - 9.0f * scale;
    const float top = cy - 6.5f * scale;
    const float width = 18.0f * scale;
    const float height = 13.0f * scale;
    const float radius = 2.1f * scale;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(iconGdiColor(color), 1.2f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::GraphicsPath outline;
    addRoundedIconRectangle(outline, left, top, width, height, radius);
    graphics.DrawPath(&pen, &outline);
    graphics.DrawLine(&pen, left + 5.3f * scale, top + 1.3f * scale,
        left + 5.3f * scale, top + height - 1.3f * scale);
}

void drawSettingsGear(HDC dc, const RECT& rect, bool dark, int dpi)
{
    const COLORREF color = dark ? RGB(225, 225, 229) : RGB(68, 72, 76);
    HFONT font = CreateFontW(-MulDiv(16, dpi, 96), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe MDL2 Assets");
    if (!font)
        return;
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT iconRect = rect;
    DrawTextW(dc, L"\xE713", -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}


void drawShare(HDC dc, const RECT& rect, bool dark = false)
{
    const COLORREF color = dark ? RGB(225, 225, 229) : RGB(67, 71, 75);
    const float scale = std::max(0.75f,
        static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) / 30.0f);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(iconGdiColor(color), 1.45f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    graphics.DrawLine(&pen, cx, cy + 1.0f * scale, cx, cy - 7.5f * scale);
    graphics.DrawLine(&pen, cx, cy - 7.5f * scale,
        cx - 4.1f * scale, cy - 3.6f * scale);
    graphics.DrawLine(&pen, cx, cy - 7.5f * scale,
        cx + 4.1f * scale, cy - 3.6f * scale);

    const Gdiplus::PointF tray[] = {
        { cx - 7.0f * scale, cy + 0.5f * scale },
        { cx - 7.0f * scale, cy + 6.7f * scale },
        { cx + 7.0f * scale, cy + 6.7f * scale },
        { cx + 7.0f * scale, cy + 0.5f * scale }
    };
    graphics.DrawLines(&pen, tray, static_cast<INT>(std::size(tray)));
}


void drawTabOverview(HDC dc, const RECT& rect, bool dark)
{
    const COLORREF color = dark ? RGB(225, 225, 229) : RGB(67, 71, 75);
    const COLORREF fillColor = dark ? RGB(57, 57, 61) : RGB(255, 255, 255);
    const float scale = std::max(0.75f,
        static_cast<float>(rect.bottom - rect.top) / 30.0f);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float boxWidth = 13.0f * scale;
    const float boxHeight = 12.0f * scale;
    const float offset = 2.4f * scale;
    const float radius = 3.0f * scale;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(iconGdiColor(color), 1.45f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush frontFill(iconGdiColor(fillColor));

    Gdiplus::GraphicsPath rear;
    addRoundedIconRectangle(rear,
        cx - boxWidth / 2.0f - offset,
        cy - boxHeight / 2.0f - offset,
        boxWidth, boxHeight, radius);
    graphics.DrawPath(&pen, &rear);

    Gdiplus::GraphicsPath front;
    addRoundedIconRectangle(front,
        cx - boxWidth / 2.0f + offset,
        cy - boxHeight / 2.0f + offset,
        boxWidth, boxHeight, radius);
    graphics.FillPath(&frontFill, &front);
    graphics.DrawPath(&pen, &front);
}


void drawDownload(HDC dc, const RECT& rect, bool dark = false)
{
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return;

    const COLORREF color = dark ? RGB(225, 225, 229) : RGB(67, 71, 75);
    const float scale = std::max(0.7f,
        static_cast<float>(std::min(rect.right - rect.left, rect.bottom - rect.top)) / 30.0f);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(iconGdiColor(color), 1.2f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    // Compact Safari-like arrow into an open, rounded download tray.
    const Gdiplus::PointF tip(cx, cy + 2.0f * scale);
    graphics.DrawLine(&pen, cx, cy - 6.6f * scale, tip.X, tip.Y);
    graphics.DrawLine(&pen, cx - 3.4f * scale, cy - 1.4f * scale, tip.X, tip.Y);
    graphics.DrawLine(&pen, tip.X, tip.Y, cx + 3.4f * scale, cy - 1.4f * scale);

    const float trayLeft = cx - 6.4f * scale;
    const float trayRight = cx + 6.4f * scale;
    const float trayTop = cy + 0.3f * scale;
    const float trayBottom = cy + 6.0f * scale;
    const float corner = 1.5f * scale;
    Gdiplus::GraphicsPath tray;
    tray.StartFigure();
    tray.AddLine(trayLeft, trayTop, trayLeft, trayBottom - corner);
    tray.AddBezier(
        Gdiplus::PointF(trayLeft, trayBottom - corner),
        Gdiplus::PointF(trayLeft, trayBottom - corner * 0.2f),
        Gdiplus::PointF(trayLeft + corner * 0.2f, trayBottom),
        Gdiplus::PointF(trayLeft + corner, trayBottom));
    tray.AddLine(trayLeft + corner, trayBottom, trayRight - corner, trayBottom);
    tray.AddBezier(
        Gdiplus::PointF(trayRight - corner, trayBottom),
        Gdiplus::PointF(trayRight - corner * 0.2f, trayBottom),
        Gdiplus::PointF(trayRight, trayBottom - corner * 0.2f),
        Gdiplus::PointF(trayRight, trayBottom - corner));
    tray.AddLine(trayRight, trayBottom - corner, trayRight, trayTop);
    graphics.DrawPath(&pen, &tray);
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


void drawTrafficLight(HDC dc, int x, int y, int size, COLORREF color)
{
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const float inset = std::max(0.35f, size * 0.035f);
    const Gdiplus::RectF circle(
        static_cast<float>(x) + inset, static_cast<float>(y) + inset,
        static_cast<float>(size) - inset * 2.0f, static_cast<float>(size) - inset * 2.0f);
    const COLORREF borderColor = RGB(
        std::max(0, static_cast<int>(GetRValue(color)) - 28),
        std::max(0, static_cast<int>(GetGValue(color)) - 28),
        std::max(0, static_cast<int>(GetBValue(color)) - 28));
    Gdiplus::SolidBrush fill(iconGdiColor(color));
    Gdiplus::Pen outline(iconGdiColor(borderColor), 0.65f);
    graphics.FillEllipse(&fill, circle);
    graphics.DrawEllipse(&outline, circle);
}

void drawPlus(HDC dc, const RECT& rect, bool dark = false)
{
    const COLORREF color = dark ? RGB(225, 225, 229) : RGB(67, 71, 75);
    const float scale = std::max(0.75f,
        static_cast<float>(rect.bottom - rect.top) / 30.0f);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float arm = 5.4f * scale;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(iconGdiColor(color), 1.45f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawLine(&pen, cx - arm, cy, cx + arm, cy);
    graphics.DrawLine(&pen, cx, cy - arm, cx, cy + arm);
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

void drawWebsiteSettings(HDC dc, const RECT& rect, bool dark = false)
{
    const COLORREF color = dark ? RGB(225, 225, 229) : RGB(68, 72, 76);
    const float scale = std::max(0.7f, static_cast<float>(rect.bottom - rect.top) / 28.0f);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float halfWidth = 5.7f * scale;
    const float knobRadius = 1.55f * scale;
    const float rows[] = { cy - 4.7f * scale, cy, cy + 4.7f * scale };
    const float knobX[] = { cx - 1.8f * scale, cx + 2.7f * scale, cx - 3.2f * scale };

    // Safari-style website/page controls are light slider strokes, not a boxed document.
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(iconGdiColor(color), 1.15f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush knobBrush(iconGdiColor(color));

    for (int i = 0; i < 3; ++i) {
        graphics.DrawLine(&pen, cx - halfWidth, rows[i], cx + halfWidth, rows[i]);
        graphics.FillEllipse(&knobBrush, knobX[i] - knobRadius, rows[i] - knobRadius,
            knobRadius * 2.0f, knobRadius * 2.0f);
    }
}

LRESULT CALLBACK addressBarProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK addressSuggestionsProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK findEditProcedure(HWND, UINT, WPARAM, LPARAM);

struct TabState {
    BrowserState* browser { nullptr };
    WKRetainPtr<WKViewRef> view;
    std::wstring title { L"New Tab" };
    std::wstring activeUrl;
    std::wstring internalPage { L"start" };
    int settingsSection { 0 };
    bool hasInternalThemeSnapshot { false };
    bool internalDarkTheme { false };
    std::wstring lastRecordedUrl;
    std::wstring lastExtensionsUrl;
    std::wstring fallbackURL;
    HICON favicon { nullptr };
    std::wstring faviconRequestURL;
    HBITMAP previewBitmap { nullptr };
    int previewWidth { 0 };
    int previewHeight { 0 };

    ~TabState()
    {
        if (previewBitmap)
            DeleteObject(previewBitmap);
    }
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
    HWND addressSuggestionWindow { nullptr };
    std::vector<VisitEntry> addressSuggestionEntries;
    int selectedAddressSuggestion { -1 };
    bool addressEditing { false };
    bool windowCounted { false };
    bool privateBrowsing { false };
    bool customMaximized { false };
    RECT restoredWindowRect { };
    bool webFullscreen { false };
    bool fullscreenRestoreWasMaximized { false };
    RECT fullscreenRestoreWindowRect { };
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
    HMENU downloadsPopupMenu { nullptr };
    bool downloadsAnimationTimerActive { false };
    bool lastDownloadsButtonVisible { false };
    int hoveredToolbarButton { -1 };
    float toolbarHoverOpacity[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    bool mouseTrackingForChrome { false };
    bool chromeAnimationTimerActive { false };
    bool tabSwitchAnimating { false };
    size_t previousActiveTab { 0 };
    ULONGLONG tabSwitchAnimationStart { 0 };
    bool sidebarOpen { false };
    bool tabOverviewVisible { false };
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
    // Safari's toolbar has its own appearance; keep System mode light by default
    // so Windows dark mode does not flatten the browser chrome into a dark strip.
    bool chromeDarkTheme() const { return themeMode == ThemeMode::Dark; }

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

        // The pinned Windows WebKit port disables the JavaScript Fullscreen API by default.
        WKPreferencesSetFullScreenEnabled(preferences.get(), true);
        // Allow sites to query capabilities from the decoders exposed by this build.
        WKPreferencesSetMediaCapabilitiesEnabled(preferences.get(), true);
        WKPreferencesSetVP9DecoderEnabled(preferences.get(), true);
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
            else if (tab.internalPage == L"settings") loadSettingsPage(tab);
            else if (tab.internalPage == L"settings") loadSettingsPage(tab);
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

    int titleBarHeight() const { return webFullscreen ? 0 : scaleForDpi(window, kTitleBarHeight); }
    int toolbarHeight() const { return webFullscreen ? 0 : scaleForDpi(window, kToolbarHeight); }
    int tabsBarHeight() const { return webFullscreen || tabs.size() <= 1 ? 0 : scaleForDpi(window, kTabsBarHeight); }
    int contentTop() const { return titleBarHeight() + toolbarHeight() + tabsBarHeight(); }

    void enterWebFullscreen()
    {
        if (webFullscreen || !window)
            return;

        GetWindowRect(window, &fullscreenRestoreWindowRect);
        fullscreenRestoreWasMaximized = customMaximized;
        webFullscreen = true;
        if (addressEditing)
            endAddressEditing();

        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info { };
        info.cbSize = sizeof(info);
        if (monitor && GetMonitorInfoW(monitor, &info)) {
            SetWindowPos(window, HWND_TOP,
                info.rcMonitor.left, info.rcMonitor.top,
                info.rcMonitor.right - info.rcMonitor.left,
                info.rcMonitor.bottom - info.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        resize();
        SetForegroundWindow(window);
        InvalidateRect(window, nullptr, TRUE);
    }

    void exitWebFullscreen()
    {
        if (!webFullscreen || !window)
            return;

        webFullscreen = false;
        if (fullscreenRestoreWindowRect.right > fullscreenRestoreWindowRect.left
            && fullscreenRestoreWindowRect.bottom > fullscreenRestoreWindowRect.top) {
            SetWindowPos(window, HWND_TOP,
                fullscreenRestoreWindowRect.left, fullscreenRestoreWindowRect.top,
                fullscreenRestoreWindowRect.right - fullscreenRestoreWindowRect.left,
                fullscreenRestoreWindowRect.bottom - fullscreenRestoreWindowRect.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        customMaximized = fullscreenRestoreWasMaximized;
        resize();
        InvalidateRect(window, nullptr, TRUE);
    }

    RECT trafficLightHitRect(int index) const
    {
        if (index < 0 || index >= 3)
            return { 0, 0, 0, 0 };

        // Use the same coordinates as the painted traffic lights. The padded
        // hit region is shared by WM_NCHITTEST and WM_LBUTTONDOWN so Windows
        // doesn't treat clicks on the circles as a draggable caption.
        const int pad = scaleForDpi(window, kToolbarHorizontalPadding);
        const int light = scaleForDpi(window, kTrafficLightSize);
        const int lightGap = scaleForDpi(window, kTrafficLightGap);
        const int hitPad = scaleForDpi(window, 6);
        const int x = pad + index * (light + lightGap);
        const int y = titleBarHeight() + (toolbarHeight() - light) / 2;
        return { x - hitPad, y - hitPad, x + light + hitPad, y + light + hitPad };
    }

    RECT sidebarButtonRect() const
    {
        const int pad = scaleForDpi(window, kToolbarHorizontalPadding);
        const int light = scaleForDpi(window, kTrafficLightSize);
        const int lightGap = scaleForDpi(window, kTrafficLightGap);
        const int width = scaleForDpi(window, 54);
        const int height = scaleForDpi(window, 28);
        const int top = titleBarHeight() + (toolbarHeight() - height) / 2;
        const int left = pad + light * 3 + lightGap * 2 + scaleForDpi(window, 11);
        return { left, top, left + width, top + height };
    }

    RECT sidebarSettingsRect() const
    {
        RECT client { };
        GetClientRect(window, &client);
        const int width = scaleForDpi(window, 286);
        const int margin = scaleForDpi(window, 12);
        const int height = scaleForDpi(window, 52);
        const int bottomMargin = scaleForDpi(window, 8);
        return { margin, client.bottom - height, width - margin, client.bottom - bottomMargin };
    }

    RECT navigationPillRect() const
    {
        RECT sidebar = sidebarButtonRect();
        const int gap = scaleForDpi(window, kToolbarGap);
        const int button = scaleForDpi(window, kToolbarButtonSize + 8);
        return { sidebar.right + gap * 2, sidebar.top,
            sidebar.right + gap * 2 + button * 2 + gap * 2, sidebar.bottom };
    }

    RECT tabBarItemRect(size_t index) const
    {
        RECT client { };
        GetClientRect(window, &client);
        const size_t count = tabs.size();
        if (!count || index >= count)
            return { 0, 0, 0, 0 };

        const int gap = scaleForDpi(window, 8);
        const int sidePadding = scaleForDpi(window, 12);
        const int available = std::max(1, static_cast<int>(client.right) - sidePadding * 2
            - gap * static_cast<int>(count - 1));
        const int maxWidth = scaleForDpi(window, 280);
        const int tabWidth = std::max(scaleForDpi(window, 112),
            std::min(maxWidth, available / static_cast<int>(count)));
        const int groupWidth = tabWidth * static_cast<int>(count) + gap * static_cast<int>(count - 1);
        const int groupLeft = std::max(sidePadding, (static_cast<int>(client.right) - groupWidth) / 2);
        const int rowTop = titleBarHeight() + toolbarHeight();
        const int rowHeight = tabsBarHeight();
        const int inset = scaleForDpi(window, 2);
        const int left = groupLeft + static_cast<int>(index) * (tabWidth + gap);
        return { left, rowTop + inset, left + tabWidth, rowTop + rowHeight - inset };
    }


    bool downloadsButtonVisible() const
    {
        return std::any_of(downloads.begin(), downloads.end(), [](const DownloadEntry& item) {
            return !item.finished && !item.failed && !item.cancelled;
        });
    }

    RECT rightToolbarButtonRect(int index) const
    {
        RECT client { };
        GetClientRect(window, &client);
        const int pad = scaleForDpi(window, kToolbarHorizontalPadding);
        const int button = scaleForDpi(window, kToolbarButtonSize + 6);
        const int gap = scaleForDpi(window, kToolbarGap);
        const bool showDownloads = downloadsButtonVisible();
        const int buttonCount = showDownloads ? 4 : 3;
        if (index < 0 || index > 3 || (!showDownloads && index == 1))
            return { 0, 0, 0, 0 };

        // Keep logical indices stable for routing: Share, Downloads, New Tab,
        // Tab Overview. When Downloads is hidden, close the gap and shift later
        // controls one slot to the left.
        const int physicalIndex = (!showDownloads && index > 1) ? index - 1 : index;
        const int left = client.right - pad - (buttonCount - physicalIndex) * button
            - (buttonCount - physicalIndex - 1) * gap;
        const int height = scaleForDpi(window, 30);
        const int top = titleBarHeight() + (toolbarHeight() - height) / 2;
        return { left, top, left + button, top + height };
    }

    void startChromeAnimationTimer()
    {
        if (!window || chromeAnimationTimerActive)
            return;
        if (SetTimer(window, kChromeAnimationTimerId, 16, nullptr))
            chromeAnimationTimerActive = true;
    }

    void updateToolbarHover(POINT point)
    {
        int nextHovered = -1;
        const int toolbarTop = titleBarHeight();
        const int toolbarBottom = toolbarTop + toolbarHeight();
        if (point.y >= toolbarTop && point.y < toolbarBottom) {
            for (int i = 0; i < 4; ++i) {
                const RECT button = rightToolbarButtonRect(i);
                if (button.right > button.left && button.bottom > button.top && PtInRect(&button, point)) {
                    nextHovered = i;
                    break;
                }
            }
        }
        if (nextHovered == hoveredToolbarButton)
            return;
        hoveredToolbarButton = nextHovered;
        startChromeAnimationTimer();
    }

    float tabSwitchEase() const
    {
        if (!tabSwitchAnimating)
            return 1.0f;
        const ULONGLONG elapsed = GetTickCount64() - tabSwitchAnimationStart;
        const float t = std::clamp(static_cast<float>(elapsed) / 180.0f, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void animateChrome()
    {
        if (!window)
            return;

        bool continueAnimation = false;
        for (int i = 0; i < 4; ++i) {
            const RECT button = rightToolbarButtonRect(i);
            const float target = hoveredToolbarButton == i && button.right > button.left ? 1.0f : 0.0f;
            const float delta = target - toolbarHoverOpacity[i];
            if (delta > -0.015f && delta < 0.015f) {
                toolbarHoverOpacity[i] = target;
            } else {
                toolbarHoverOpacity[i] += delta * 0.28f;
                continueAnimation = true;
            }
        }

        if (tabSwitchAnimating) {
            if (GetTickCount64() - tabSwitchAnimationStart >= 180) {
                tabSwitchAnimating = false;
            } else {
                continueAnimation = true;
            }
        }

        RECT client { };
        GetClientRect(window, &client);
        RECT chromeRect { 0, 0, client.right,
            titleBarHeight() + toolbarHeight() + tabsBarHeight() };
        InvalidateRect(window, &chromeRect, FALSE);

        if (!continueAnimation) {
            KillTimer(window, kChromeAnimationTimerId);
            chromeAnimationTimerActive = false;
        }
    }

    RECT tabOverviewAreaRect() const
    {
        RECT client { };
        GetClientRect(window, &client);
        const int left = sidebarOpen ? scaleForDpi(window, 286) : 0;
        return { left, contentTop(), client.right, client.bottom };
    }

    int tabOverviewColumns() const
    {
        const RECT area = tabOverviewAreaRect();
        const int margin = scaleForDpi(window, 24);
        const int gap = scaleForDpi(window, 18);
        const int minimumCardWidth = scaleForDpi(window, 270);
        const int available = std::max(1, static_cast<int>(area.right - area.left) - margin * 2);
        const int columnsForViewport = std::clamp((available + gap) / (minimumCardWidth + gap), 1, 4);
        return std::max(1, std::min(columnsForViewport, static_cast<int>(tabs.size())));
    }

    RECT tabOverviewNewTabRect() const
    {
        const RECT area = tabOverviewAreaRect();
        const int margin = scaleForDpi(window, 24);
        const int width = scaleForDpi(window, 112);
        const int height = scaleForDpi(window, 32);
        return { area.right - margin - width, area.top + scaleForDpi(window, 12),
            area.right - margin, area.top + scaleForDpi(window, 12) + height };
    }

    RECT tabOverviewCardRect(size_t index) const
    {
        const RECT area = tabOverviewAreaRect();
        const int columns = tabOverviewColumns();
        const int margin = scaleForDpi(window, 24);
        const int gap = scaleForDpi(window, 18);
        const int headerHeight = scaleForDpi(window, 58);
        const int areaWidth = static_cast<int>(area.right - area.left);
        const int availableCardWidth = std::max(scaleForDpi(window, 150),
            (areaWidth - margin * 2 - gap * (columns - 1)) / columns);
        const int cardWidth = std::min(scaleForDpi(window, 360), availableCardWidth);
        const int cardsWidth = cardWidth * columns + gap * (columns - 1);
        const int groupLeft = area.left + std::max(0, (areaWidth - cardsWidth) / 2);
        const int cardHeight = scaleForDpi(window, 226);
        const int column = static_cast<int>(index % static_cast<size_t>(columns));
        const int row = static_cast<int>(index / static_cast<size_t>(columns));
        const int left = groupLeft + column * (cardWidth + gap);
        const int top = area.top + headerHeight + row * (cardHeight + gap);
        return { left, top, left + cardWidth, top + cardHeight };
    }

    RECT tabOverviewCloseRect(size_t index) const
    {
        const RECT card = tabOverviewCardRect(index);
        const int size = scaleForDpi(window, 24);
        const int inset = scaleForDpi(window, 8);
        return { card.right - inset - size, card.top + inset,
            card.right - inset, card.top + inset + size };
    }

    void captureTabThumbnail(TabState& tab)
    {
        HWND viewWindow = WKViewGetWindow(tab.view.get());
        if (!viewWindow)
            return;

        RECT client { };
        if (!GetClientRect(viewWindow, &client))
            return;
        const int sourceWidth = static_cast<int>(client.right - client.left);
        const int sourceHeight = static_cast<int>(client.bottom - client.top);
        if (sourceWidth < 80 || sourceHeight < 60)
            return;

        HDC sourceDC = GetDC(viewWindow);
        if (!sourceDC)
            return;
        HDC captureDC = CreateCompatibleDC(sourceDC);
        HBITMAP captureBitmap = CreateCompatibleBitmap(sourceDC, sourceWidth, sourceHeight);
        if (!captureDC || !captureBitmap) {
            if (captureBitmap) DeleteObject(captureBitmap);
            if (captureDC) DeleteDC(captureDC);
            ReleaseDC(viewWindow, sourceDC);
            return;
        }

        HGDIOBJ oldCapture = SelectObject(captureDC, captureBitmap);
        BOOL captured = PrintWindow(viewWindow, captureDC, PW_CLIENTONLY | 0x00000002);
        if (!captured)
            captured = BitBlt(captureDC, 0, 0, sourceWidth, sourceHeight, sourceDC, 0, 0, SRCCOPY);

        if (captured) {
            const double factor = std::min(480.0 / sourceWidth, 270.0 / sourceHeight);
            const int targetWidth = std::max(1, static_cast<int>(sourceWidth * factor));
            const int targetHeight = std::max(1, static_cast<int>(sourceHeight * factor));
            HDC previewDC = CreateCompatibleDC(sourceDC);
            HBITMAP preview = CreateCompatibleBitmap(sourceDC, targetWidth, targetHeight);
            if (previewDC && preview) {
                HGDIOBJ oldPreview = SelectObject(previewDC, preview);
                SetStretchBltMode(previewDC, HALFTONE);
                StretchBlt(previewDC, 0, 0, targetWidth, targetHeight,
                    captureDC, 0, 0, sourceWidth, sourceHeight, SRCCOPY);
                SelectObject(previewDC, oldPreview);
                if (tab.previewBitmap)
                    DeleteObject(tab.previewBitmap);
                tab.previewBitmap = preview;
                tab.previewWidth = targetWidth;
                tab.previewHeight = targetHeight;
            } else if (preview) {
                DeleteObject(preview);
            }
            if (previewDC)
                DeleteDC(previewDC);
        }

        SelectObject(captureDC, oldCapture);
        DeleteObject(captureBitmap);
        DeleteDC(captureDC);
        ReleaseDC(viewWindow, sourceDC);
    }

    bool handleTabOverviewHit(POINT point)
    {
        if (!tabOverviewVisible)
            return false;
        const RECT area = tabOverviewAreaRect();
        if (!PtInRect(&area, point))
            return false;

        const RECT newTab = tabOverviewNewTabRect();
        if (PtInRect(&newTab, point)) {
            addTab(true);
            return true;
        }

        for (size_t i = 0; i < tabs.size(); ++i) {
            const RECT card = tabOverviewCardRect(i);
            if (!PtInRect(&card, point))
                continue;
            const RECT close = tabOverviewCloseRect(i);
            if (PtInRect(&close, point))
                closeTab(i);
            else
                activateTab(i);
            return true;
        }
        return true;
    }

    void drawTabOverviewPage(HDC dc)
    {
        const RECT area = tabOverviewAreaRect();
        const bool dark = chromeDarkTheme();
        HBRUSH background = CreateSolidBrush(dark ? RGB(30, 30, 32) : RGB(240, 241, 244));
        FillRect(dc, &area, background);
        DeleteObject(background);

        const int dpi = windowDpi(window);
        HFONT headingFont = CreateFontW(-MulDiv(16, dpi, 96), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT bodyFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT urlFont = CreateFontW(-MulDiv(10, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, headingFont));
        SetBkMode(dc, TRANSPARENT);

        RECT titleRect { area.left + scaleForDpi(window, 24), area.top + scaleForDpi(window, 10),
            area.right - scaleForDpi(window, 160), area.top + scaleForDpi(window, 40) };
        SetTextColor(dc, dark ? RGB(240, 240, 242) : RGB(40, 43, 47));
        DrawTextW(dc, L"Tab Overview", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const RECT newTab = tabOverviewNewTabRect();
        HBRUSH newTabBrush = CreateSolidBrush(dark ? RGB(58, 58, 61) : RGB(255, 255, 255));
        HPEN newTabPen = CreatePen(PS_SOLID, 1, dark ? RGB(78, 78, 82) : RGB(213, 216, 220));
        HBRUSH oldNewBrush = static_cast<HBRUSH>(SelectObject(dc, newTabBrush));
        HPEN oldNewPen = static_cast<HPEN>(SelectObject(dc, newTabPen));
        RoundRect(dc, newTab.left, newTab.top, newTab.right, newTab.bottom,
            scaleForDpi(window, 8), scaleForDpi(window, 8));
        SelectObject(dc, oldNewBrush);
        SelectObject(dc, oldNewPen);
        DeleteObject(newTabBrush);
        DeleteObject(newTabPen);
        RECT plusRect { newTab.left + scaleForDpi(window, 8), newTab.top,
            newTab.left + scaleForDpi(window, 28), newTab.bottom };
        drawPlus(dc, plusRect, dark);
        HFONT oldBodyFont = static_cast<HFONT>(SelectObject(dc, bodyFont));
        RECT newTabText { newTab.left + scaleForDpi(window, 30), newTab.top,
            newTab.right - scaleForDpi(window, 6), newTab.bottom };
        SetTextColor(dc, dark ? RGB(235, 235, 238) : RGB(47, 51, 55));
        DrawTextW(dc, L"New Tab", -1, &newTabText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldBodyFont);

        const int cardHeight = scaleForDpi(window, 226);
        const int previewInset = scaleForDpi(window, 8);
        const int metaHeight = scaleForDpi(window, 58);
        for (size_t i = 0; i < tabs.size(); ++i) {
            const RECT card = tabOverviewCardRect(i);
            if (card.top >= area.bottom || card.bottom <= area.top)
                continue;

            HBRUSH cardBrush = CreateSolidBrush(dark
                ? (i == activeTab ? RGB(53, 53, 56) : RGB(43, 43, 46))
                : (i == activeTab ? RGB(255, 255, 255) : RGB(253, 253, 254)));
            HPEN cardPen = CreatePen(PS_SOLID, 1, dark
                ? (i == activeTab ? RGB(102, 126, 164) : RGB(69, 69, 73))
                : (i == activeTab ? RGB(133, 166, 211) : RGB(220, 222, 226)));
            HBRUSH oldCardBrush = static_cast<HBRUSH>(SelectObject(dc, cardBrush));
            HPEN oldCardPen = static_cast<HPEN>(SelectObject(dc, cardPen));
            RoundRect(dc, card.left, card.top, card.right, card.bottom,
                scaleForDpi(window, 9), scaleForDpi(window, 9));
            SelectObject(dc, oldCardBrush);
            SelectObject(dc, oldCardPen);
            DeleteObject(cardBrush);
            DeleteObject(cardPen);

            RECT preview { card.left + previewInset, card.top + previewInset,
                card.right - previewInset, card.bottom - metaHeight };
            HBRUSH previewBrush = CreateSolidBrush(dark ? RGB(35, 35, 37) : RGB(248, 249, 251));
            FillRect(dc, &preview, previewBrush);
            DeleteObject(previewBrush);

            if (tabs[i]->previewBitmap && tabs[i]->previewWidth > 0 && tabs[i]->previewHeight > 0) {
                HDC previewDC = CreateCompatibleDC(dc);
                if (previewDC) {
                    HGDIOBJ oldPreview = SelectObject(previewDC, tabs[i]->previewBitmap);
                    const double scale = std::min(
                        static_cast<double>(preview.right - preview.left) / tabs[i]->previewWidth,
                        static_cast<double>(preview.bottom - preview.top) / tabs[i]->previewHeight);
                    const int drawWidth = static_cast<int>(tabs[i]->previewWidth * scale);
                    const int drawHeight = static_cast<int>(tabs[i]->previewHeight * scale);
                    const int drawLeft = preview.left + ((preview.right - preview.left) - drawWidth) / 2;
                    const int drawTop = preview.top + ((preview.bottom - preview.top) - drawHeight) / 2;
                    SetStretchBltMode(dc, HALFTONE);
                    StretchBlt(dc, drawLeft, drawTop, drawWidth, drawHeight,
                        previewDC, 0, 0, tabs[i]->previewWidth, tabs[i]->previewHeight, SRCCOPY);
                    SelectObject(previewDC, oldPreview);
                    DeleteDC(previewDC);
                }
            } else {
                RECT fakeBar { preview.left, preview.top, preview.right, preview.top + scaleForDpi(window, 18) };
                HBRUSH fakeBarBrush = CreateSolidBrush(dark ? RGB(48, 48, 51) : RGB(236, 238, 241));
                FillRect(dc, &fakeBar, fakeBarBrush);
                DeleteObject(fakeBarBrush);
                if (tabs[i]->favicon) {
                    const int iconSize = scaleForDpi(window, 20);
                    DrawIconEx(dc, preview.left + scaleForDpi(window, 12),
                        preview.top + scaleForDpi(window, 32), tabs[i]->favicon,
                        iconSize, iconSize, 0, nullptr, DI_NORMAL);
                }
                RECT line1 { preview.left + scaleForDpi(window, 42), preview.top + scaleForDpi(window, 34),
                    preview.right - scaleForDpi(window, 20), preview.top + scaleForDpi(window, 42) };
                RECT line2 { preview.left + scaleForDpi(window, 42), preview.top + scaleForDpi(window, 48),
                    preview.right - scaleForDpi(window, 50), preview.top + scaleForDpi(window, 55) };
                HBRUSH lineBrush = CreateSolidBrush(dark ? RGB(72, 72, 75) : RGB(223, 226, 230));
                FillRect(dc, &line1, lineBrush);
                FillRect(dc, &line2, lineBrush);
                DeleteObject(lineBrush);
            }

            RECT closeRect = tabOverviewCloseRect(i);
            HBRUSH closeBrush = CreateSolidBrush(dark ? RGB(68, 68, 71) : RGB(255, 255, 255));
            HPEN closePen = CreatePen(PS_SOLID, 1, dark ? RGB(95, 95, 98) : RGB(220, 222, 226));
            HBRUSH oldCloseBrush = static_cast<HBRUSH>(SelectObject(dc, closeBrush));
            HPEN oldClosePen = static_cast<HPEN>(SelectObject(dc, closePen));
            RoundRect(dc, closeRect.left, closeRect.top, closeRect.right, closeRect.bottom,
                closeRect.right - closeRect.left, closeRect.bottom - closeRect.top);
            SelectObject(dc, oldCloseBrush);
            SelectObject(dc, oldClosePen);
            DeleteObject(closeBrush);
            DeleteObject(closePen);
            drawClose(dc, closeRect, dark);

            RECT titleText { card.left + scaleForDpi(window, 12), card.bottom - metaHeight + scaleForDpi(window, 4),
                card.right - scaleForDpi(window, 12), card.bottom - scaleForDpi(window, 30) };
            SelectObject(dc, bodyFont);
            SetTextColor(dc, dark ? RGB(240, 240, 242) : RGB(40, 43, 47));
            const std::wstring title = tabs[i]->title.empty() ? L"New Tab" : tabs[i]->title;
            DrawTextW(dc, title.c_str(), -1, &titleText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            RECT urlText { card.left + scaleForDpi(window, 12), card.bottom - scaleForDpi(window, 26),
                card.right - scaleForDpi(window, 12), card.bottom - scaleForDpi(window, 8) };
            SelectObject(dc, urlFont);
            SetTextColor(dc, dark ? RGB(162, 162, 167) : RGB(119, 124, 130));
            const std::wstring url = tabs[i]->activeUrl.empty() ? L"about:blank" : tabs[i]->activeUrl;
            DrawTextW(dc, url.c_str(), -1, &urlText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        SelectObject(dc, oldFont);
        DeleteObject(headingFont);
        DeleteObject(bodyFont);
        DeleteObject(urlFont);
    }


    TabState* active()
    {
        if (tabs.empty())
            return nullptr;
        if (activeTab >= tabs.size())
            activeTab = tabs.size() - 1;
        return tabs[activeTab].get();
    }

    std::wstring addressDisplayText() const
    {
        if (tabs.empty() || activeTab >= tabs.size())
            return {};
        const std::wstring& url = tabs[activeTab]->activeUrl;
        if (url.empty() || url == L"about:blank")
            return {};

        const size_t scheme = url.find(L"://");
        if (scheme == std::wstring::npos)
            return url.rfind(L"file:", 0) == 0 ? L"Local File" : url;

        const size_t hostStart = scheme + 3;
        const size_t hostEnd = url.find_first_of(L"/?#", hostStart);
        std::wstring host = url.substr(hostStart,
            hostEnd == std::wstring::npos ? std::wstring::npos : hostEnd - hostStart);
        const size_t credentials = host.rfind(L'@');
        if (credentials != std::wstring::npos)
            host.erase(0, credentials + 1);
        if (host.rfind(L"www.", 0) == 0)
            host.erase(0, 4);
        return host;
    }

    void beginAddressEditing()
    {
        if (!addressBar)
            return;
        const TabState* tab = tabs.empty() || activeTab >= tabs.size() ? nullptr : tabs[activeTab].get();
        const std::wstring current = tab && tab->activeUrl != L"about:blank" ? tab->activeUrl : L"";
        SetWindowTextW(addressBar, current.c_str());
        addressEditing = true;
        ShowWindow(addressBar, SW_SHOW);
        SetFocus(addressBar);
        SendMessageW(addressBar, EM_SETSEL, 0, -1);
    }

    void endAddressEditing()
    {
        hideAddressSuggestions();
        addressEditing = false;
        if (addressBar)
            ShowWindow(addressBar, SW_HIDE);
        if (window)
            InvalidateRect(window, nullptr, FALSE);
    }

    void hideAddressSuggestions()
    {
        selectedAddressSuggestion = -1;
        if (addressSuggestionWindow && IsWindow(addressSuggestionWindow))
            ShowWindow(addressSuggestionWindow, SW_HIDE);
    }

    void moveAddressSuggestionSelection(int direction)
    {
        if (!addressEditing || addressSuggestionEntries.empty()) {
            if (addressEditing && addressBar) {
                wchar_t text[4096] { };
                GetWindowTextW(addressBar, text, static_cast<int>(std::size(text)));
                updateAddressSuggestions(text);
            }
        }
        if (addressSuggestionEntries.empty() || !addressSuggestionWindow
            || !IsWindowVisible(addressSuggestionWindow))
            return;

        const int count = static_cast<int>(addressSuggestionEntries.size());
        if (selectedAddressSuggestion < 0)
            selectedAddressSuggestion = direction > 0 ? 0 : count - 1;
        else
            selectedAddressSuggestion = (selectedAddressSuggestion + direction + count) % count;
        InvalidateRect(addressSuggestionWindow, nullptr, TRUE);
    }

    void chooseAddressSuggestion(size_t index)
    {
        if (index >= addressSuggestionEntries.size())
            return;
        const std::wstring target = addressSuggestionEntries[index].url;
        hideAddressSuggestions();
        if (!target.empty()) {
            navigateInput(target);
            endAddressEditing();
        }
    }

    void updateAddressSuggestions(const std::wstring& queryText)
    {
        if (!addressEditing || !addressBar || privateBrowsing) {
            hideAddressSuggestions();
            return;
        }

        auto lowercase = [](std::wstring value) {
            std::transform(value.begin(), value.end(), value.begin(),
                [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
            return value;
        };
        auto hostFor = [](const std::wstring& url) {
            const size_t scheme = url.find(L"://");
            if (scheme == std::wstring::npos)
                return url;
            const size_t begin = scheme + 3;
            const size_t end = url.find_first_of(L"/?#:", begin);
            std::wstring host = url.substr(begin, end == std::wstring::npos ? end : end - begin);
            if (host.rfind(L"www.", 0) == 0)
                host.erase(0, 4);
            return host;
        };

        std::wstring query = queryText;
        while (!query.empty() && iswspace(query.front()))
            query.erase(query.begin());
        while (!query.empty() && iswspace(query.back()))
            query.pop_back();
        const std::wstring loweredQuery = lowercase(query);
        addressSuggestionEntries.clear();
        selectedAddressSuggestion = -1;

        if (loweredQuery.empty()) {
            // With an empty field show the most frequently visited distinct sites.
            const auto frequent = frequentVisits(32);
            std::vector<std::wstring> seenHosts;
            for (const auto& entry : frequent) {
                const std::wstring host = lowercase(hostFor(entry.url));
                if (host.empty() || std::find(seenHosts.begin(), seenHosts.end(), host) != seenHosts.end())
                    continue;
                seenHosts.push_back(host);
                addressSuggestionEntries.push_back(entry);
                if (addressSuggestionEntries.size() >= 6)
                    break;
            }
        } else {
            struct RankedVisit {
                int rank { 99 };
                VisitEntry entry;
            };
            std::vector<RankedVisit> candidates;
            candidates.reserve(visits.size());
            for (const auto& entry : visits) {
                const std::wstring url = lowercase(entry.url);
                const std::wstring title = lowercase(entry.title);
                const std::wstring host = lowercase(hostFor(entry.url));
                int rank = 99;
                if (url.rfind(loweredQuery, 0) == 0)
                    rank = 0;
                else if (host.rfind(loweredQuery, 0) == 0)
                    rank = 1;
                else if (title.rfind(loweredQuery, 0) == 0)
                    rank = 2;
                else if (host.find(loweredQuery) != std::wstring::npos)
                    rank = 3;
                else if (title.find(loweredQuery) != std::wstring::npos)
                    rank = 4;
                else if (url.find(loweredQuery) != std::wstring::npos)
                    rank = 5;
                if (rank < 99)
                    candidates.push_back({ rank, entry });
            }

            std::sort(candidates.begin(), candidates.end(), [](const RankedVisit& a, const RankedVisit& b) {
                if (a.rank != b.rank)
                    return a.rank < b.rank;
                if (a.entry.visits != b.entry.visits)
                    return a.entry.visits > b.entry.visits;
                return a.entry.lastVisited > b.entry.lastVisited;
            });
            for (const auto& candidate : candidates) {
                if (std::find_if(addressSuggestionEntries.begin(), addressSuggestionEntries.end(),
                    [&](const VisitEntry& entry) { return entry.url == candidate.entry.url; })
                    != addressSuggestionEntries.end())
                    continue;
                addressSuggestionEntries.push_back(candidate.entry);
                if (addressSuggestionEntries.size() >= 6)
                    break;
            }
        }

        if (addressSuggestionEntries.empty()) {
            hideAddressSuggestions();
            return;
        }

        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW cls { };
            cls.cbSize = sizeof(cls);
            cls.lpfnWndProc = addressSuggestionsProcedure;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.lpszClassName = L"AuroraAddressSuggestions";
            if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return;
            classRegistered = true;
        }

        if (!addressSuggestionWindow || !IsWindow(addressSuggestionWindow)) {
            addressSuggestionWindow = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                L"AuroraAddressSuggestions", L"",
                WS_POPUP,
                0, 0, 400, 300,
                window, nullptr, GetModuleHandleW(nullptr), this);
            if (!addressSuggestionWindow)
                return;
        }

        RECT editRect { };
        GetWindowRect(addressBar, &editRect);
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo { };
        monitorInfo.cbSize = sizeof(monitorInfo);
        RECT work { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        if (monitor && GetMonitorInfoW(monitor, &monitorInfo))
            work = monitorInfo.rcWork;

        const int dpi = windowDpi(window);
        const int rowHeight = MulDiv(50, dpi, 96);
        const int popupHeight = rowHeight * static_cast<int>(addressSuggestionEntries.size()) + MulDiv(2, dpi, 96);
        const int editWidth = static_cast<int>(editRect.right - editRect.left);
        const int popupWidth = std::min(static_cast<int>(work.right - work.left),
            std::max(editWidth, MulDiv(420, dpi, 96)));
        int popupLeft = static_cast<int>(editRect.left) - (popupWidth - editWidth) / 2;
        popupLeft = std::clamp(popupLeft, static_cast<int>(work.left),
            std::max(static_cast<int>(work.left), static_cast<int>(work.right) - popupWidth));
        int popupTop = static_cast<int>(editRect.bottom);
        if (popupTop + popupHeight > work.bottom)
            popupTop = static_cast<int>(editRect.top) - popupHeight;
        popupTop = std::clamp(popupTop, static_cast<int>(work.top),
            std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - popupHeight));

        SetWindowPos(addressSuggestionWindow, HWND_TOPMOST, popupLeft, popupTop,
            popupWidth, popupHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(addressSuggestionWindow, nullptr, TRUE);
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
            -MulDiv(12, windowDpi(window), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
        if (addressSuggestionWindow && IsWindow(addressSuggestionWindow))
            DestroyWindow(addressSuggestionWindow);
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
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER,
            0, 0, 0, 0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandAddress)),
            GetModuleHandleW(nullptr),
            nullptr
        );

        SendMessageW(addressBar, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        SendMessageW(addressBar, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search or enter website name"));
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

    void loadSettingsPage(TabState& tab, int requestedSection = -1)
    {
        if (requestedSection >= 0)
            tab.settingsSection = std::clamp(requestedSection, 0, 5);
        tab.internalPage = L"settings";
        tab.hasInternalThemeSnapshot = true;
        tab.internalDarkTheme = darkTheme();
        const bool dark = darkTheme();
        const std::string theme = themeMode == ThemeMode::System ? "system" : (themeMode == ThemeMode::Light ? "light" : "dark");
        const std::string engine = toUTF8(searchEngine);
        const std::string home = toUTF8(htmlEscape(homePage));
        const std::string downloadsPath = toUTF8(htmlEscape(downloadLocation));
        const std::string systemSelected = theme == "system" ? " selected" : "";
        const std::string lightSelected = theme == "light" ? " selected" : "";
        const std::string darkSelected = theme == "dark" ? " selected" : "";
        const std::string googleSelected = engine == "Google" ? " selected" : "";
        const std::string bingSelected = engine == "Bing" ? " selected" : "";
        const std::string duckSelected = engine == "DuckDuckGo" ? " selected" : "";
        const std::string activeGeneral = tab.settingsSection == 0 ? " active" : "";
        const std::string activeTabs = tab.settingsSection == 1 ? " active" : "";
        const std::string activeSearch = tab.settingsSection == 2 ? " active" : "";
        const std::string activeWebsites = tab.settingsSection == 3 ? " active" : "";
        const std::string activePrivacy = tab.settingsSection == 4 ? " active" : "";
        const std::string activeAdvanced = tab.settingsSection == 5 ? " active" : "";
        auto checked = [](bool value) { return value ? " checked" : ""; };

        std::string html = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings</title><style>
:root{color-scheme:)HTML" + std::string(dark ? "dark" : "light") + R"HTML(;--bg:)HTML"
            + (dark ? "#1c1c1e" : "#f5f5f7") + R"HTML(;--panel:)HTML" + (dark ? "#262628" : "#fff")
            + R"HTML(;--bar:)HTML" + (dark ? "#222224" : "#f5f5f7") + R"HTML(;--text:)HTML"
            + (dark ? "#f5f5f7" : "#1d1d1f") + R"HTML(;--muted:)HTML" + (dark ? "#a1a1a6" : "#6e6e73")
            + R"HTML(;--line:)HTML" + (dark ? "#414144" : "#dedee2") + R"HTML(;--selected:)HTML"
            + (dark ? "#3b3b3f" : "#e6e6ea") + R"HTML(;--blue:)HTML" + (dark ? "#8ab4f8" : "#1769d2")
            + R"HTML(}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:var(--bg);color:var(--text)}
form{margin:0}
.shell{width:min(1040px,calc(100% - 48px));margin:24px auto 32px;background:var(--panel);border:1px solid var(--line);border-radius:20px;overflow:hidden;box-shadow:0 8px 36px rgba(0,0,0,.08)}
.top{padding:12px 20px 0;background:var(--bar);border-bottom:1px solid var(--line)}
.topline{display:flex;align-items:center;justify-content:space-between;min-height:26px;margin-bottom:10px}
.topline strong{font-size:14px;font-weight:600}
.done{display:inline-block;text-decoration:none;border:0;border-radius:9px;padding:7px 13px;background:var(--selected);color:var(--text);font:inherit;cursor:pointer}
.categories{display:flex;align-items:stretch;justify-content:center;gap:3px;overflow-x:auto;padding-bottom:10px}
.category{appearance:none;flex:1 1 0;min-width:72px;max-width:108px;border:0;border-radius:13px;background:transparent;color:var(--muted);padding:8px 4px 7px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px;cursor:pointer;font:inherit}
.category svg{width:23px;height:23px;stroke:currentColor;fill:none;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
.category span{font-size:11px;white-space:nowrap}
.category.active{background:var(--selected);color:var(--text)}
.content{max-width:790px;margin:0 auto;padding:30px 34px 34px}
h1{font-size:23px;font-weight:600;letter-spacing:-.35px;margin:0 0 5px}
.sub{color:var(--muted);font-size:13px;line-height:1.5;margin:0 0 28px}
.panel{display:none}.panel.active{display:block}
.field{display:grid;grid-template-columns:minmax(180px,230px) minmax(0,1fr);align-items:center;gap:18px;padding:15px 0;border-bottom:1px solid var(--line)}
.field:first-child{border-top:1px solid var(--line)}
.field label,.field .fieldname{font-size:13px}
input[type=text],select{width:100%;min-width:0;min-height:34px;border:1px solid var(--line);border-radius:9px;background:var(--bar);color:var(--text);padding:7px 10px;font:inherit;font-size:13px}
.checks{border:1px solid var(--line);border-radius:13px;overflow:hidden;margin-top:6px}
.checkrow{display:flex;align-items:center;gap:10px;padding:13px 14px;border-bottom:1px solid var(--line);font-size:13px}
.checkrow:last-child{border-bottom:0}
input[type=checkbox]{width:15px;height:15px;accent-color:var(--blue)}
.help{font-size:12px;color:var(--muted);line-height:1.55;margin:12px 0 0}
.actions{display:flex;gap:9px;align-items:center;flex-wrap:wrap;margin-top:22px}
.action{border:1px solid var(--line);border-radius:9px;background:var(--bar);color:var(--text);padding:9px 13px;font:inherit;font-size:13px;cursor:pointer}
.action.primary{border-color:var(--blue);background:var(--blue);color:white}
.status{color:var(--muted);font-size:12px;margin-left:5px}
@media(max-width:720px){.shell{width:calc(100% - 16px);margin:8px auto;border-radius:14px}.top{padding:10px 8px 0}.category{min-width:66px}.content{padding:24px 18px}.field{grid-template-columns:1fr;gap:8px}}
</style></head><body>
<div class="shell"><form action="aurora-settings://save" method="get"><header class="top"><div class="topline"><strong>Aurora Settings</strong><a class="done" href="aurora-settings://close">Done</a></div>
<nav class="categories" aria-label="Settings categories">
<button class="category__ACTIVE_GENERAL__" type="submit" formaction="aurora-settings://navigate" name="section" value="0"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1-1.7 1.7-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.5v.2h-2.4v-.2a1.7 1.7 0 0 0-1-1.5 1.7 1.7 0 0 0-1.9.3l-.1.1L8 17l.1-.1a1.7 1.7 0 0 0 .3-1.9 1.7 1.7 0 0 0-1.5-1H6.7v-2.4h.2a1.7 1.7 0 0 0 1.5-1 1.7 1.7 0 0 0-.3-1.9L8 8.6l1.7-1.7.1.1a1.7 1.7 0 0 0 1.9.3 1.7 1.7 0 0 0 1-1.5v-.2h2.4v.2a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1 1.7 1.7-.1.1a1.7 1.7 0 0 0-.3 1.9 1.7 1.7 0 0 0 1.5 1h.2V14h-.2a1.7 1.7 0 0 0-1.4 1z"/></svg><span>General</span></button>
<button class="category__ACTIVE_TABS__" type="submit" formaction="aurora-settings://navigate" name="section" value="1"><svg viewBox="0 0 24 24"><rect x="3" y="4" width="13" height="16" rx="2"/><rect x="8" y="2" width="13" height="16" rx="2"/></svg><span>Tabs</span></button>
<button class="category__ACTIVE_SEARCH__" type="submit" formaction="aurora-settings://navigate" name="section" value="2"><svg viewBox="0 0 24 24"><circle cx="10.5" cy="10.5" r="6.5"/><path d="m15.5 15.5 5 5"/></svg><span>Search</span></button>
<button class="category__ACTIVE_WEBSITES__" type="submit" formaction="aurora-settings://navigate" name="section" value="3"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/></svg><span>Websites</span></button>
<button class="category__ACTIVE_PRIVACY__" type="submit" formaction="aurora-settings://navigate" name="section" value="4"><svg viewBox="0 0 24 24"><path d="M12 3 20 6v5c0 5-3.4 8.2-8 10-4.6-1.8-8-5-8-10V6z"/><path d="m8.5 12 2.2 2.2 4.8-5"/></svg><span>Privacy</span></button>
<button class="category__ACTIVE_ADVANCED__" type="submit" formaction="aurora-settings://navigate" name="section" value="5"><svg viewBox="0 0 24 24"><path d="M4 6h16M4 12h16M4 18h16"/><circle cx="9" cy="6" r="2" fill="var(--bar)"/><circle cx="15" cy="12" r="2" fill="var(--bar)"/><circle cx="8" cy="18" r="2" fill="var(--bar)"/></svg><span>Advanced</span></button>
</nav></header><main class="content">
<section class="panel__ACTIVE_SECTION_GENERAL__" data-section="general"><h1>General</h1><p class="sub">Choose your homepage and where downloads are saved.</p>
<div class="field"><label for="home">Homepage</label><input id="home" type="text" name="home" value="__HOME__"></div>
<div class="field"><label for="downloads">Downloads location</label><input id="downloads" type="text" name="downloads" value="__DOWNLOADS__"></div>
<p class="help">Enter a folder path that already exists on this PC. Leave the location blank to use the Windows Downloads folder.</p>
</section>
<section class="panel__ACTIVE_SECTION_TABS__" data-section="appearance"><h1>Tabs</h1><p class="sub">Choose the browser appearance and tab behavior.</p>
<div class="field"><label for="theme">Appearance</label><select id="theme" name="theme"><option value="system"__THEME_SYSTEM__>Automatic (follow Windows)</option><option value="light"__THEME_LIGHT__>Light</option><option value="dark"__THEME_DARK__>Dark</option></select></div>
<p class="help">This setting changes Aurora's browser chrome and built-in pages.</p></section>
<section class="panel__ACTIVE_SECTION_SEARCH__" data-section="search"><h1>Search</h1><p class="sub">Choose the search engine used by Smart Search.</p>
<div class="field"><label for="engine">Default search engine</label><select id="engine" name="engine"><option value="Google"__ENGINE_GOOGLE__>Google</option><option value="Bing"__ENGINE_BING__>Bing</option><option value="DuckDuckGo"__ENGINE_DUCK__>DuckDuckGo</option></select></div></section>
<section class="panel__ACTIVE_SECTION_WEBSITES__" data-section="websites"><h1>Websites</h1><p class="sub">Choose the default permissions used for websites.</p>
<div class="checks">
<label class="checkrow"><input type="hidden" name="javascript" value="0"><input id="javascript" type="checkbox" name="javascript" value="1"__JAVASCRIPT__>Enable JavaScript</label>
<label class="checkrow"><input type="hidden" name="images" value="0"><input id="images" type="checkbox" name="images" value="1"__IMAGES__>Load images automatically</label>
<label class="checkrow"><input type="hidden" name="popups" value="0"><input id="popups" type="checkbox" name="popups" value="1"__POPUPS__>Allow pop-up windows</label>
<label class="checkrow"><input type="hidden" name="storage" value="0"><input id="storage" type="checkbox" name="storage" value="1"__STORAGE__>Allow website storage</label></div></section>
<section class="panel__ACTIVE_SECTION_PRIVACY__" data-section="privacy"><h1>Privacy</h1><p class="sub">Manage the local data Aurora stores while you browse.</p>
<div class="actions"><a class="action" href="aurora-settings://clear-history">Clear History…</a><a class="action" href="aurora-settings://clear-data">Clear Website Data…</a></div>
<p class="help">Clearing website data may sign you out of websites.</p></section>
<section class="panel__ACTIVE_SECTION_ADVANCED__" data-section="advanced"><h1>Advanced</h1><p class="sub">Options for development and diagnostics.</p>
<div class="checks"><label class="checkrow"><input type="hidden" name="developer" value="0"><input id="developer" type="checkbox" name="developer" value="1"__DEVELOPER__>Enable Web Inspector / developer tools</label></div>
<p class="help">Some inspector functions depend on the pinned Windows WebKit build.</p></section>
<div class="actions"><button class="action primary" type="submit">Save Changes</button></div>
</main></form></div></body></html>)HTML";

        const std::pair<const std::string, const std::string> replacements[] = {
            { "__HOME__", home }, { "__DOWNLOADS__", downloadsPath },
            { "__THEME_SYSTEM__", systemSelected }, { "__THEME_LIGHT__", lightSelected }, { "__THEME_DARK__", darkSelected },
            { "__ENGINE_GOOGLE__", googleSelected }, { "__ENGINE_BING__", bingSelected }, { "__ENGINE_DUCK__", duckSelected },
            { "__JAVASCRIPT__", checked(javascriptEnabled) }, { "__IMAGES__", checked(imagesEnabled) },
            { "__POPUPS__", checked(allowPopups) }, { "__STORAGE__", checked(localStorageEnabled) },
            { "__DEVELOPER__", checked(developerToolsEnabled) },
            { "__ACTIVE_GENERAL__", activeGeneral }, { "__ACTIVE_TABS__", activeTabs },
            { "__ACTIVE_SEARCH__", activeSearch }, { "__ACTIVE_WEBSITES__", activeWebsites },
            { "__ACTIVE_PRIVACY__", activePrivacy }, { "__ACTIVE_ADVANCED__", activeAdvanced },
            { "__ACTIVE_SECTION_GENERAL__", activeGeneral }, { "__ACTIVE_SECTION_TABS__", activeTabs },
            { "__ACTIVE_SECTION_SEARCH__", activeSearch }, { "__ACTIVE_SECTION_WEBSITES__", activeWebsites },
            { "__ACTIVE_SECTION_PRIVACY__", activePrivacy }, { "__ACTIVE_SECTION_ADVANCED__", activeAdvanced }
        };
        for (const auto& replacement : replacements) {
            size_t offset = 0;
            while ((offset = html.find(replacement.first, offset)) != std::string::npos) {
                html.replace(offset, replacement.first.size(), replacement.second);
                offset += replacement.second.size();
            }
        }
        loadLocalHTML(tab, html);
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
        navigationClient.decidePolicyForNavigationAction = decidePolicyForNavigationAction;
        navigationClient.decidePolicyForNavigationResponse = decidePolicyForNavigationResponse;
        navigationClient.didFailProvisionalNavigation = didFailProvisionalNavigation;
        navigationClient.navigationActionDidBecomeDownload = navigationActionDidBecomeDownload;
        navigationClient.navigationResponseDidBecomeDownload = navigationResponseDidBecomeDownload;
        navigationClient.contextMenuDidCreateDownload = contextMenuDidCreateDownload;
        WKPageSetPageNavigationClient(page, &navigationClient.base);

        WKPageFullScreenClientV0 fullscreenClient { };
        fullscreenClient.base.version = 0;
        fullscreenClient.base.clientInfo = this;
        fullscreenClient.willEnterFullScreen = browserWillEnterFullScreen;
        fullscreenClient.beganEnterFullScreen = browserBeganEnterFullScreen;
        fullscreenClient.exitFullScreen = browserExitFullScreen;
        fullscreenClient.beganExitFullScreen = browserBeganExitFullScreen;
        WKPageSetFullScreenClientForTesting(page, &fullscreenClient.base);

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

        // Save the previous page before addTab changes activeTab. Otherwise
        // newly-created tabs skip the thumbnail that Safari's overview displays.
        if (select && !tabOverviewVisible && activeTab < tabs.size())
            captureTabThumbnail(*tabs[activeTab]);

        tabs.push_back(std::move(tab));
        size_t index = tabs.size() - 1;

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

        const size_t previouslyActive = activeTab;
        if (!tabOverviewVisible && activeTab < tabs.size() && index != activeTab)
            captureTabThumbnail(*tabs[activeTab]);
        if (addressEditing)
            endAddressEditing();
        tabOverviewVisible = false;
        activeTab = index;
        if (previouslyActive != index && previouslyActive < tabs.size()) {
            previousActiveTab = previouslyActive;
            tabSwitchAnimationStart = GetTickCount64();
            tabSwitchAnimating = true;
            startChromeAnimationTimer();
        }

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
        RECT client { };
        GetClientRect(window, &client);
        RECT chromeRect { 0, 0, client.right, titleBarHeight() + toolbarHeight() + tabsBarHeight() };
        InvalidateRect(window, &chromeRect, FALSE);
    }

    void closeTab(size_t index)
    {
        if (index >= tabs.size())
            return;

        const bool keepOverviewOpen = tabOverviewVisible;
        if (!keepOverviewOpen && index == activeTab)
            captureTabThumbnail(*tabs[index]);

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

        if (keepOverviewOpen) {
            for (const auto& tab : tabs) {
                if (HWND viewWindow = WKViewGetWindow(tab->view.get()))
                    ShowWindow(viewWindow, SW_HIDE);
            }
            if (auto* tab = active()) {
                const std::wstring text = tab->activeUrl == L"about:blank" ? L"" : tab->activeUrl;
                SetWindowTextW(addressBar, text.c_str());
            }
            resize();
            InvalidateRect(window, nullptr, TRUE);
        } else {
            activateTab(activeTab);
        }
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
        tab->hasInternalThemeSnapshot = true;
        tab->internalDarkTheme = dark;
        std::string html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Downloads</title>"
            "<style>:root{color-scheme:" + std::string(dark ? "dark" : "light") + "}*{box-sizing:border-box}body{margin:0;background:"
            + (dark ? "#1c1c1e" : "#f5f6f7") + ";color:" + (dark ? "#f5f5f7" : "#202428")
            + ";font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Arial,sans-serif}"
            ".wrap{max-width:1040px;margin:auto;padding:48px 32px 64px;animation:downloads-enter 190ms cubic-bezier(.2,.75,.25,1) both}"
            "h1{font-size:30px;font-weight:600;letter-spacing:-.4px;margin:0 0 8px}"
            ".intro{font-size:13px;color:" + std::string(dark ? "#a1a1a6" : "#6e737a") + ";line-height:1.5;margin:0 0 26px}"
            ".item{background:" + std::string(dark ? "#29292c" : "#fff") + ";border:1px solid " + (dark ? "#414145" : "#e0e3e7")
            + ";border-radius:14px;padding:17px 18px;margin:11px 0;box-shadow:0 1px 2px rgba(0,0,0,.025);animation:download-card-enter 220ms cubic-bezier(.2,.75,.25,1) both}"
            ".item:nth-of-type(2){animation-delay:18ms}.item:nth-of-type(3){animation-delay:36ms}.item:nth-of-type(4){animation-delay:54ms}"
            "@keyframes downloads-enter{from{opacity:0;transform:translateY(5px)}to{opacity:1;transform:translateY(0)}}"
            "@keyframes download-card-enter{from{opacity:0;transform:translateY(7px) scale(.995)}to{opacity:1;transform:translateY(0) scale(1)}}"
            ".name{font-size:14px;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
            ".path{font-size:12px;color:" + std::string(dark ? "#a1a1a6" : "#73777d") + ";margin-top:5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
            ".meta{font-size:12px;color:" + std::string(dark ? "#b8b8bd" : "#666b72") + ";margin-top:11px}"
            ".bar{height:4px;background:" + std::string(dark ? "#48484a" : "#e4e7eb") + ";border-radius:4px;overflow:hidden;margin-top:8px}"
            ".fill{height:100%;background:#3478d4;border-radius:4px;transition:width .15s linear}"
            ".bar.indeterminate .fill{width:28%!important;animation:download-sweep 1.15s ease-in-out infinite alternate}"
            "@keyframes download-sweep{from{transform:translateX(0)}to{transform:translateX(255%)}}"
            ".actions{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-top:14px}"
            ".button{display:inline-flex;align-items:center;justify-content:center;min-height:29px;border:1px solid " + std::string(dark ? "#55555a" : "#d7dbe0")
            + ";border-radius:8px;padding:5px 11px;background:" + (dark ? "#37373a" : "#fafbfc") + ";color:inherit;text-decoration:none;font-size:12px;font-family:inherit;cursor:pointer}"
            ".button:hover{background:" + std::string(dark ? "#454549" : "#f0f2f5") + "}"
            ".button.cancel{color:" + std::string(dark ? "#ffaaa7" : "#b42318") + ";border-color:" + (dark ? "#744340" : "#f0c8c5") + "}"
            ".button:disabled{opacity:.42;cursor:not-allowed}"
            ".empty{color:" + std::string(dark ? "#a1a1a6" : "#777") + ";padding:22px 0}"
            "</style></head><body><main class='wrap'><h1>Downloads</h1>"
            "<p class='intro'>Manage recent downloads. Pause and resume are not provided by this Windows WebKit build.</p>";
        if (downloads.empty()) {
            html += "<div class='empty'>No downloads yet.</div>";
        } else {
            for (size_t i = 0; i < downloads.size(); ++i) {
                const auto& item = downloads[i];
                const std::string id = std::to_string(i);
                long long percent = 0;
                if (item.totalBytes > 0)
                    percent = std::clamp<long long>((item.bytesWritten * 100) / item.totalBytes, 0, 100);
                const bool inProgress = !item.finished && !item.failed && !item.cancelled;
                const std::string status = item.cancelled ? "Cancelled"
                    : item.failed ? "Download failed"
                    : item.finished ? "Downloaded"
                    : (item.totalBytes > 0
                        ? "Downloading - " + std::to_string(percent) + "% (" + formatDownloadBytes(item.bytesWritten) + ")"
                        : (item.bytesWritten > 0 ? "Downloading - " + formatDownloadBytes(item.bytesWritten) + " received"
                            : "Starting download"));
                html += "<section class='item'><div class='name' title='";
                html += toUTF8(htmlEscape(item.filename));
                html += "'>";
                html += toUTF8(htmlEscape(item.filename));
                html += "</div><div class='path' title='";
                html += toUTF8(htmlEscape(item.path));
                html += "'>";
                html += toUTF8(htmlEscape(item.path));
                html += "</div><div class='meta' id='download-meta-" + id + "'>" + status + "</div>";
                html += "<div class='bar";
                if (inProgress && item.totalBytes <= 0)
                    html += " indeterminate";
                html += "' id='download-bar-" + id + "' style='display:";
                html += inProgress ? "block" : "none";
                html += "'><div class='fill' id='download-fill-" + id + "' style='width:";
                html += std::to_string(percent);
                html += "%'></div></div>";
                html += "<div class='actions'><a class='button' href='aurora-download://folder?id=" + id + "'>Show in Folder</a>";
                html += "<button class='button' id='download-pause-" + id
                    + "' type='button' disabled title='Pause/resume is not exposed by this WebKit Windows build'>Pause</button>";
                html += "<a class='button cancel' id='download-cancel-" + id + "' href='aurora-download://cancel?id=" + id + "' style='display:";
                html += inProgress ? "inline-flex" : "none";
                html += "'>Cancel</a></div></section>";
            }
        }
        html += "</main></body></html>";
        loadLocalHTML(*tab, html);
        tab->activeUrl = L"about:downloads";
        tab->title = L"Downloads";
        InvalidateRect(window, nullptr, FALSE);
    }

    void showFileInFolder(const std::wstring& path)
    {
        if (path.empty())
            return;

        std::wstring arguments = L"/select,\"" + path + L"\"";
        ShellExecuteW(window, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
    }


    bool isDownloadMenuCommand(UINT command) const
    {
        return command >= 6000 && command < 6000 + downloads.size();
    }

    bool drawDownloadsMenuItem(const DRAWITEMSTRUCT& item) const
    {
        if (item.CtlType != ODT_MENU || !isDownloadMenuCommand(item.itemID))
            return false;
        const size_t index = item.itemID - 6000;
        if (index >= downloads.size())
            return false;
        const auto& download = downloads[index];
        const bool dark = chromeDarkTheme();
        const bool selected = (item.itemState & ODS_SELECTED) != 0;
        const int dpi = windowDpi(window);
        const int leftPad = scaleForDpi(window, 16);
        const int rightPad = scaleForDpi(window, 14);
        const int topPad = scaleForDpi(window, 7);
        const bool inProgress = !download.finished && !download.failed && !download.cancelled;
        long long percent = 0;
        if (download.totalBytes > 0)
            percent = std::clamp<long long>((download.bytesWritten * 100) / download.totalBytes, 0, 100);
        const COLORREF background = selected
            ? (dark ? RGB(65, 65, 69) : RGB(239, 242, 246))
            : (dark ? RGB(39, 39, 42) : RGB(255, 255, 255));
        const COLORREF primary = dark ? RGB(243, 243, 246) : RGB(34, 38, 42);
        const COLORREF secondary = dark ? RGB(166, 167, 173) : RGB(111, 116, 123);
        HBRUSH backgroundBrush = CreateSolidBrush(background);
        FillRect(item.hDC, &item.rcItem, backgroundBrush);
        DeleteObject(backgroundBrush);
        SetBkMode(item.hDC, TRANSPARENT);
        HFONT titleFont = CreateFontW(-MulDiv(13, dpi, 96), 0, 0, 0, FW_MEDIUM,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT statusFont = CreateFontW(-MulDiv(11, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        RECT titleRect { item.rcItem.left + leftPad, item.rcItem.top + topPad,
            item.rcItem.right - rightPad, item.rcItem.top + scaleForDpi(window, 29) };
        HFONT oldFont = static_cast<HFONT>(SelectObject(item.hDC, titleFont));
        SetTextColor(item.hDC, primary);
        DrawTextW(item.hDC, download.filename.c_str(), -1, &titleRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT statusRect { item.rcItem.left + leftPad, item.rcItem.top + scaleForDpi(window, 28),
            item.rcItem.right - rightPad, item.rcItem.top + scaleForDpi(window, 47) };
        SelectObject(item.hDC, statusFont);
        SetTextColor(item.hDC, secondary);
        std::wstring status;
        if (download.cancelled)
            status = L"Cancelled";
        else if (download.failed)
            status = L"Download failed";
        else if (download.finished)
            status = L"Downloaded";
        else if (download.totalBytes > 0)
            status = L"Downloading - " + std::to_wstring(percent) + L"% ("
                + fromUTF8(formatDownloadBytes(download.bytesWritten)) + L")";
        else if (download.bytesWritten > 0)
            status = L"Downloading - " + fromUTF8(formatDownloadBytes(download.bytesWritten)) + L" received";
        else
            status = L"Starting download";
        DrawTextW(item.hDC, status.c_str(), -1, &statusRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(item.hDC, oldFont);
        DeleteObject(titleFont);
        DeleteObject(statusFont);
        if (inProgress) {
            const int barHeight = std::max(3, scaleForDpi(window, 4));
            const int barTop = item.rcItem.bottom - scaleForDpi(window, 10);
            const int barLeft = item.rcItem.left + leftPad;
            const int barRight = item.rcItem.right - rightPad;
            Gdiplus::Graphics graphics(item.hDC);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath track;
            addRoundedIconRectangle(track, static_cast<float>(barLeft), static_cast<float>(barTop),
                static_cast<float>(std::max(1, barRight - barLeft)), static_cast<float>(barHeight),
                static_cast<float>(barHeight) / 2.0f);
            Gdiplus::SolidBrush trackBrush(iconGdiColor(dark ? RGB(78, 78, 82) : RGB(230, 233, 237)));
            graphics.FillPath(&trackBrush, &track);
            const int trackWidth = std::max(1, barRight - barLeft);
            if (download.totalBytes > 0) {
                const double shownPercent = std::clamp(download.displayedProgressPercent, 0.0, 100.0);
                const float fillWidth = static_cast<float>(trackWidth * shownPercent / 100.0);
                if (fillWidth > 0.5f) {
                    Gdiplus::GraphicsPath fill;
                    addRoundedIconRectangle(fill, static_cast<float>(barLeft), static_cast<float>(barTop),
                        fillWidth, static_cast<float>(barHeight), static_cast<float>(barHeight) / 2.0f);
                    Gdiplus::SolidBrush fillBrush(iconGdiColor(RGB(75, 139, 230)));
                    graphics.FillPath(&fillBrush, &fill);
                }
            } else if (inProgress) {
                // Time-based movement is independent from transfer callback frequency.
                const int segmentWidth = std::min(trackWidth,
                    std::max(scaleForDpi(window, 64), trackWidth / 4));
                const int travel = std::max(1, trackWidth - segmentWidth);
                const ULONGLONG phase = GetTickCount64() % 1400ULL;
                const double phaseProgress = phase < 700ULL
                    ? static_cast<double>(phase) / 700.0
                    : static_cast<double>(1400ULL - phase) / 700.0;
                const int segmentLeft = barLeft + static_cast<int>(phaseProgress * travel);
                Gdiplus::GraphicsPath segment;
                addRoundedIconRectangle(segment, static_cast<float>(segmentLeft), static_cast<float>(barTop),
                    static_cast<float>(std::min(segmentWidth, barRight - segmentLeft)),
                    static_cast<float>(barHeight), static_cast<float>(barHeight) / 2.0f);
                Gdiplus::SolidBrush fillBrush(iconGdiColor(RGB(75, 139, 230)));
                graphics.FillPath(&fillBrush, &segment);
            }
        }
        HPEN dividerPen = CreatePen(PS_SOLID, 1, dark ? RGB(54, 54, 57) : RGB(241, 242, 244));
        HPEN oldPen = static_cast<HPEN>(SelectObject(item.hDC, dividerPen));
        MoveToEx(item.hDC, item.rcItem.left + leftPad, item.rcItem.bottom - 1, nullptr);
        LineTo(item.hDC, item.rcItem.right - rightPad, item.rcItem.bottom - 1);
        SelectObject(item.hDC, oldPen);
        DeleteObject(dividerPen);
        return true;
    }

    void startDownloadsAnimationTimer()
    {
        if (window && downloadsPopupMenu && !downloadsAnimationTimerActive) {
            if (SetTimer(window, kDownloadsAnimationTimerId, 16, nullptr))
                downloadsAnimationTimerActive = true;
        }
    }

    void stopDownloadsAnimationTimer()
    {
        if (window && downloadsAnimationTimerActive)
            KillTimer(window, kDownloadsAnimationTimerId);
        downloadsAnimationTimerActive = false;
    }

    void invalidateDownloadPopupRows()
    {
        if (!downloadsPopupMenu)
            return;
        HWND popupWindow = FindWindowW(L"#32768", nullptr);
        RECT popupBounds { };
        if (!popupWindow || !IsWindowVisible(popupWindow) || !GetWindowRect(popupWindow, &popupBounds))
            return;

        const size_t first = downloads.size() > 7 ? downloads.size() - 7 : 0;
        for (size_t i = first; i < downloads.size(); ++i) {
            RECT itemScreenRect { };
            if (!GetMenuItemRect(window, downloadsPopupMenu, static_cast<UINT>(downloads.size() - 1 - i), &itemScreenRect))
                continue;
            OffsetRect(&itemScreenRect, -popupBounds.left, -popupBounds.top);
            InvalidateRect(popupWindow, &itemScreenRect, FALSE);
        }
    }

    void animateDownloadsPopup()
    {
        if (!downloadsPopupMenu) {
            stopDownloadsAnimationTimer();
            return;
        }

        bool animationPending = false;
        const size_t first = downloads.size() > 7 ? downloads.size() - 7 : 0;
        for (size_t i = first; i < downloads.size(); ++i) {
            auto& item = downloads[i];
            const bool inProgress = !item.finished && !item.failed && !item.cancelled;
            if (!inProgress)
                continue;

            if (item.totalBytes > 0) {
                const double target = std::clamp(
                    static_cast<double>(item.bytesWritten) * 100.0 / static_cast<double>(item.totalBytes),
                    0.0, 100.0);
                const double delta = target - item.displayedProgressPercent;
                if (delta > -0.12 && delta < 0.12)
                    item.displayedProgressPercent = target;
                else {
                    item.displayedProgressPercent += delta * 0.24;
                    animationPending = true;
                }
            } else {
                // Unknown content length needs a continuously moving segment.
                animationPending = true;
            }
        }

        if (animationPending)
            invalidateDownloadPopupRows();
        else
            stopDownloadsAnimationTimer();
    }

    void refreshDownloadsPresentation()
    {
        if (!window)
            return;
        for (const auto& tab : tabs) {
            if (tab->internalPage != L"downloads")
                continue;
            std::string script = "(()=>{";
            for (size_t i = 0; i < downloads.size(); ++i) {
                const auto& item = downloads[i];
                const std::string id = std::to_string(i);
                long long percent = 0;
                if (item.totalBytes > 0)
                    percent = std::clamp<long long>((item.bytesWritten * 100) / item.totalBytes, 0, 100);
                const bool inProgress = !item.finished && !item.failed && !item.cancelled;
                const std::string status = item.cancelled ? "Cancelled"
                    : item.failed ? "Download failed"
                    : item.finished ? "Downloaded"
                    : (item.totalBytes > 0
                        ? "Downloading - " + std::to_string(percent) + "% (" + formatDownloadBytes(item.bytesWritten) + ")"
                        : (item.bytesWritten > 0 ? "Downloading - " + formatDownloadBytes(item.bytesWritten) + " received"
                            : "Starting download"));
                script += "var s=document.getElementById('download-meta-" + id + "');if(s)s.textContent='" + status + "';";
                script += "var f=document.getElementById('download-fill-" + id + "');if(f)f.style.width='" + std::to_string(percent) + "%';";
                script += "var b=document.getElementById('download-bar-" + id + "');if(b){b.style.display='" + std::string(inProgress ? "block" : "none") + "';b.classList.toggle('indeterminate'," + std::string(inProgress && item.totalBytes <= 0 ? "true" : "false") + ");}";
                script += "var c=document.getElementById('download-cancel-" + id + "');if(c)c.style.display='" + std::string(inProgress ? "inline-flex" : "none") + "';";
            }
            script += "})();";
            auto javascript = adoptWK(WKStringCreateWithUTF8CString(script.c_str()));
            if (javascript)
                WKPageEvaluateJavaScriptInMainFrame(WKViewGetPage(tab->view.get()), javascript.get(), nullptr, nullptr);
        }
        const bool shouldShowDownloadsButton = downloadsButtonVisible();
        if (shouldShowDownloadsButton != lastDownloadsButtonVisible) {
            RECT client { };
            GetClientRect(window, &client);
            const int pad = scaleForDpi(window, kToolbarHorizontalPadding);
            const int button = scaleForDpi(window, kToolbarButtonSize + 6);
            const int gap = scaleForDpi(window, kToolbarGap);
            const int fullGroupWidth = button * 4 + gap * 3;
            RECT actionArea {
                std::max(0, static_cast<int>(client.right) - pad - fullGroupWidth - scaleForDpi(window, 6)),
                titleBarHeight(),
                client.right,
                titleBarHeight() + toolbarHeight()
            };
            // Repaint only when the button appears/disappears, not for every chunk.
            InvalidateRect(window, &actionArea, FALSE);
            lastDownloadsButtonVisible = shouldShowDownloadsButton;
        }

        if (downloadsPopupMenu) {
            if (downloadsButtonVisible())
                startDownloadsAnimationTimer();
            invalidateDownloadPopupRows();
        }
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
                AppendMenuW(downloadsMenu, MF_OWNERDRAW, 6000 + static_cast<UINT>(i),
                    reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(i)));
            }
        }
        AppendMenuW(downloadsMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(downloadsMenu, MF_STRING, 5996, L"Show All Downloads");
        const RECT button = rightToolbarButtonRect(1);
        POINT point { button.right, button.bottom };
        ClientToScreen(window, &point);
        downloadsPopupMenu = downloadsMenu;
        if (downloadsButtonVisible())
            startDownloadsAnimationTimer();
        TrackPopupMenu(downloadsMenu, TPM_RIGHTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
        stopDownloadsAnimationTimer();
        downloadsPopupMenu = nullptr;
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
        WKPageGetContentsAsMHTMLData(request->page, request, savePageDataCompleted);
    }

    void printPage()
    {
        auto* tab = active();
        if (!tab || tab->activeUrl.empty() || tab->activeUrl == L"about:blank"
            || tab->activeUrl.rfind(L"about:", 0) == 0
            || (tab->activeUrl.rfind(L"https://", 0) != 0 && tab->activeUrl.rfind(L"http://", 0) != 0)) {
            MessageBoxW(window, L"Open a regular webpage before printing.", L"Print", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const wchar_t prompt[] =
            L"This Windows WebKit build does not expose native page-to-PDF printing.\n\n"
            L"Aurora can open this page in your default browser, where you can use Ctrl+P. "
            L"The other browser may not share Aurora's login session. Continue?";
        if (MessageBoxW(window, prompt, L"Print", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;

        HINSTANCE result = ShellExecuteW(window, L"open", tab->activeUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            MessageBoxW(window, L"Windows could not open this page in your default browser.", L"Print", MB_OK | MB_ICONWARNING);
            return;
        }

        MessageBoxW(window, L"The page was opened in your default browser. Use Ctrl+P there to print it.", L"Print", MB_OK | MB_ICONINFORMATION);
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
        POINT point { client.right - scaleForDpi(window, 190), contentTop() };
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


    void showWebsiteSettingsMenu()
    {
        HMENU pageMenu = CreatePopupMenu();
        if (!pageMenu)
            return;
        std::wstring siteLabel = L"Website";
        if (auto* tab = active()) {
            const std::wstring& url = tab->activeUrl;
            const size_t scheme = url.find(L"://");
            if (scheme != std::wstring::npos) {
                const size_t hostStart = scheme + 3;
                const size_t hostEnd = url.find_first_of(L"/?#", hostStart);
                if (hostStart < url.size())
                    siteLabel = url.substr(hostStart,
                        hostEnd == std::wstring::npos ? std::wstring::npos : hostEnd - hostStart);
            }
        }
        AppendMenuW(pageMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, siteLabel.c_str());
        AppendMenuW(pageMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(pageMenu, MF_STRING, kPrivacySiteInfo, L"Connection Information");
        AppendMenuW(pageMenu, MF_STRING, kCommandReader, L"Reader Mode");
        HMENU zoomMenu = CreatePopupMenu();
        if (zoomMenu) {
            AppendMenuW(zoomMenu, MF_STRING, kMenuZoomOut, L"Zoom Out");
            AppendMenuW(zoomMenu, MF_STRING, kMenuResetZoom, L"Actual Size");
            AppendMenuW(zoomMenu, MF_STRING, kMenuZoomIn, L"Zoom In");
            AppendMenuW(pageMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(zoomMenu), L"Page Zoom");
        }
        AppendMenuW(pageMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(pageMenu, MF_STRING, kPrivacyWebsiteSettings, L"Website Settings...");
        AppendMenuW(pageMenu, MF_STRING, kCommandReload, L"Reload Page");
        const RECT pill = addressPillRect();
        POINT point { pill.left + scaleForDpi(window, 16), pill.bottom };
        ClientToScreen(window, &point);
        TrackPopupMenu(pageMenu, TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
        DestroyMenu(pageMenu);
    }

    void showTabOverviewMenu()
    {
        if (tabs.empty())
            return;

        if (tabOverviewVisible) {
            activateTab(activeTab);
            return;
        }

        if (addressEditing)
            endAddressEditing();
        if (auto* tab = active())
            captureTabThumbnail(*tab);

        tabOverviewVisible = true;
        for (const auto& tab : tabs) {
            if (HWND viewWindow = WKViewGetWindow(tab->view.get()))
                ShowWindow(viewWindow, SW_HIDE);
        }
        SetFocus(window);
        InvalidateRect(window, nullptr, TRUE);
    }

    void showSidebarSelectionMenu()
    {
        HMENU popup = CreatePopupMenu();
        if (!popup)
            return;
        AppendMenuW(popup, MF_STRING | (sidebarSection == 0 ? MF_CHECKED : 0), kSidebarBookmarks, L"Bookmarks");
        AppendMenuW(popup, MF_STRING | (sidebarSection == 1 ? MF_CHECKED : 0), kSidebarReadingList, L"Reading List");
        AppendMenuW(popup, MF_STRING | (sidebarSection == 2 ? MF_CHECKED : 0), kSidebarHistory, L"History");

        RECT button = sidebarButtonRect();
        POINT point { button.left, button.bottom };
        ClientToScreen(window, &point);
        const UINT command = TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
            point.x, point.y, 0, window, nullptr);
        DestroyMenu(popup);
        if (!command)
            return;

        if (command == kSidebarBookmarks) sidebarSection = 0;
        else if (command == kSidebarReadingList) sidebarSection = 1;
        else if (command == kSidebarHistory) sidebarSection = 2;
        sidebarOpen = true;
        resize();
        InvalidateRect(window, nullptr, TRUE);
    }

    void executeWebEditCommand(const char* command)
    {
        auto* tab = active();
        if (!tab || !command)
            return;
        auto commandString = adoptWK(WKStringCreateWithUTF8CString(command));
        if (commandString)
            WKPageExecuteCommand(WKViewGetPage(tab->view.get()), commandString.get());
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
            contentTop()
        };
        ClientToScreen(window, &point);

        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN, point.x, point.y, 0, window, nullptr);
    }

    RECT addressPillRect() const
    {
        RECT client { };
        GetClientRect(window, &client);

        const int gap = scaleForDpi(window, kToolbarGap);
        const RECT navigation = navigationPillRect();
        // Reserve equal space for Safari-style Share, New Tab, and Tab Overview.
        const RECT firstRightButton = rightToolbarButtonRect(0);
        const int minLeft = navigation.right + gap * 2;
        const int maxRight = firstRightButton.left - gap * 2;
        const int available = std::max(0, maxRight - minLeft);
        const int width = std::min(scaleForDpi(window, 630), available);
        int left = (static_cast<int>(client.right) - width) / 2;
        left = std::clamp(left, minLeft, std::max(minLeft, maxRight - width));

        const int height = scaleForDpi(window, 30);
        const int top = titleBarHeight() + (toolbarHeight() - height) / 2;
        return { left, top, left + width, top + height };
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

        int contentTop = this->contentTop();
        int contentLeft = webFullscreen ? 0 : (sidebarOpen ? scaleForDpi(window, 286) : 0);
        for (const auto& tab : tabs) {
            HWND viewWindow = WKViewGetWindow(tab->view.get());
            if (!viewWindow)
                continue;

            // Repaint the WebKit child after changing the sidebar offset. Without
            // repainting here, the old viewport can remain visible over the sidebar
            // until another WebKit paint or window event occurs.
            MoveWindow(
                viewWindow,
                contentLeft,
                contentTop,
                std::max<int>(0, static_cast<int>(client.right - contentLeft)),
                std::max<int>(0, static_cast<int>(client.bottom - contentTop)),
                TRUE
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
            const size_t index = command - 6000;
            if (index < downloads.size()) {
                const auto& item = downloads[index];
                if (item.finished && !item.failed) {
                    if (GetFileAttributesW(item.path.c_str()) != INVALID_FILE_ATTRIBUTES)
                        ShellExecuteW(window, L"open", item.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    else
                        MessageBoxW(window, L"The downloaded file could not be found.", L"Downloads — Aurora", MB_OK | MB_ICONWARNING);
                } else if (!item.path.empty() && GetFileAttributesW(item.path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    showFileInFolder(item.path);
                }
            }
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
            showWebsiteSettingsMenu();
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
            beginAddressEditing();
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
        case kPrivacyWebsiteSettings:
            openBrowserSettings(this, 3);
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
        case kEditUndo: executeWebEditCommand("Undo"); break;
        case kEditRedo: executeWebEditCommand("Redo"); break;
        case kEditCut: executeWebEditCommand("Cut"); break;
        case kEditCopy: executeWebEditCommand("Copy"); break;
        case kEditPaste: executeWebEditCommand("Paste"); break;
        case kEditSelectAll: executeWebEditCommand("SelectAll"); break;
        case kSidebarBookmarks: sidebarSection = 0; sidebarOpen = true; resize(); break;
        case kSidebarReadingList: sidebarSection = 1; sidebarOpen = true; resize(); break;
        case kSidebarHistory: sidebarSection = 2; sidebarOpen = true; resize(); break;
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
        const int top = contentTop() + scaleForDpi(window, 8);
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

    bool toolbarHasInteractiveHit(POINT point) const
    {
        const int top = titleBarHeight();
        if (point.y < top || point.y >= top + toolbarHeight())
            return false;

        for (int i = 0; i < 3; ++i) {
            const RECT hitRect = trafficLightHitRect(i);
            if (PtInRect(&hitRect, point))
                return true;
        }

        const RECT sidebar = sidebarButtonRect();
        const RECT navigation = navigationPillRect();
        const RECT address = addressPillRect();
        if (PtInRect(&sidebar, point) || PtInRect(&navigation, point) || PtInRect(&address, point))
            return true;

        for (int i = 0; i < 4; ++i) {
            const RECT buttonRect = rightToolbarButtonRect(i);
            if (PtInRect(&buttonRect, point))
                return true;
        }
        return false;
    }

    bool handleTopBarHit(POINT point)
    {
        const int top = titleBarHeight() + toolbarHeight();
        const int stripHeight = tabsBarHeight();
        if (!stripHeight || point.y < top || point.y >= top + stripHeight)
            return false;

        for (size_t i = 0; i < tabs.size(); ++i) {
            const RECT tabRect = tabBarItemRect(i);
            if (PtInRect(&tabRect, point))
                return handleTabHit(point, i, tabRect.left,
                    static_cast<int>(tabRect.right - tabRect.left));
        }
        return false;
    }

    bool handleToolbarHit(POINT point)
    {
        const int top = titleBarHeight();
        const int bottom = titleBarHeight() + toolbarHeight();
        if (point.y < top || point.y >= bottom)
            return false;

        const RECT closeWindowHit = trafficLightHitRect(0);
        const RECT minimizeHit = trafficLightHitRect(1);
        const RECT maximizeHit = trafficLightHitRect(2);
        if (PtInRect(&closeWindowHit, point)) { handleCommand(kCommandCloseWindow); return true; }
        if (PtInRect(&minimizeHit, point)) { handleCommand(kCommandMinimize); return true; }
        if (PtInRect(&maximizeHit, point)) { handleCommand(kCommandMaximize); return true; }

        RECT sidebar = sidebarButtonRect();
        if (PtInRect(&sidebar, point)) {
            if (point.x >= sidebar.right - scaleForDpi(window, 18))
                showSidebarSelectionMenu();
            else
                handleCommand(kCommandSidebar);
            return true;
        }

        RECT navigation = navigationPillRect();
        RECT back { navigation.left, navigation.top, navigation.left + (navigation.right - navigation.left) / 2, navigation.bottom };
        RECT forward { back.right, navigation.top, navigation.right, navigation.bottom };
        if (PtInRect(&back, point)) { handleCommand(kCommandBack); return true; }
        if (PtInRect(&forward, point)) { handleCommand(kCommandForward); return true; }

        RECT pill = addressPillRect();
        RECT privacyRect { pill.left, pill.top, pill.left + scaleForDpi(window, 30), pill.bottom };
        if (PtInRect(&privacyRect, point)) { handleCommand(kCommandPrivacy); return true; }
        RECT reloadRect { pill.right - scaleForDpi(window, 32), pill.top, pill.right, pill.bottom };
        if (PtInRect(&reloadRect, point)) { handleCommand(kCommandReload); return true; }
        if (PtInRect(&pill, point)) {
            beginAddressEditing();
            return true;
        }

        const RECT shareButton = rightToolbarButtonRect(0);
        const RECT downloadButton = rightToolbarButtonRect(1);
        const RECT newTabButton = rightToolbarButtonRect(2);
        const RECT overviewButton = rightToolbarButtonRect(3);
        if (PtInRect(&shareButton, point)) { handleCommand(kCommandShare); return true; }
        if (downloadsButtonVisible() && PtInRect(&downloadButton, point)) { handleCommand(kCommandDownloads); return true; }
        if (PtInRect(&newTabButton, point)) { handleCommand(kCommandNewTab); return true; }
        if (PtInRect(&overviewButton, point)) { handleCommand(kCommandTabOverview); return true; }
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

        const bool dark = chromeDarkTheme();
        int width = scaleForDpi(window, 286);
        int top = contentTop();
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
        HFONT font = CreateFontW(-MulDiv(10, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT secondaryFont = CreateFontW(-MulDiv(9, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT headingFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
        SetBkMode(dc, TRANSPARENT);

        RECT heading { scaleForDpi(window, 18), top + scaleForDpi(window, 14),
            width - scaleForDpi(window, 54), top + scaleForDpi(window, 44) };
        HFONT oldHeadingFont = static_cast<HFONT>(SelectObject(dc, headingFont));
        SetTextColor(dc, dark ? RGB(240, 240, 242) : RGB(35, 39, 42));
        DrawTextW(dc, L"Library", -1, &heading, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldHeadingFont);

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

        int rowTop = top + scaleForDpi(window, 88);
        int rowHeight = scaleForDpi(window, 42);
        const RECT settingsRect = sidebarSettingsRect();
        const int listBottom = settingsRect.top - scaleForDpi(window, 8);
        const size_t visibleRows = listBottom > rowTop
            ? std::min<size_t>(10, static_cast<size_t>((listBottom - rowTop) / rowHeight))
            : 0;
        size_t count = sidebarSection == 0 ? bookmarks.size() : (sidebarSection == 1 ? readingList.size() : visits.size());

        if (!count) {
            RECT empty { scaleForDpi(window, 18), rowTop, width - scaleForDpi(window, 18), rowTop + scaleForDpi(window, 42) };
            SetTextColor(dc, dark ? RGB(160, 160, 165) : RGB(125, 130, 134));
            const wchar_t* emptyText = sidebarSection == 0 ? L"No bookmarks yet." : (sidebarSection == 1 ? L"No saved pages yet." : L"No browsing history.");
            DrawTextW(dc, emptyText, -1, &empty, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        } else {
            SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
            if (sidebarSection == 0) {
                for (size_t i = 0; i < std::min(visibleRows, bookmarks.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 2), width - scaleForDpi(window, 16), y + scaleForDpi(window, 20) };
                    DrawTextW(dc, bookmarks[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 22), width - scaleForDpi(window, 16), y + scaleForDpi(window, 39) };
                    SelectObject(dc, secondaryFont);
                    SetTextColor(dc, dark ? RGB(161, 161, 166) : RGB(126, 131, 135));
                    DrawTextW(dc, bookmarks[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SelectObject(dc, font);
                    SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
                }
            } else if (sidebarSection == 1) {
                for (size_t i = 0; i < std::min(visibleRows, readingList.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 2), width - scaleForDpi(window, 16), y + scaleForDpi(window, 20) };
                    DrawTextW(dc, readingList[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 22), width - scaleForDpi(window, 16), y + scaleForDpi(window, 39) };
                    SelectObject(dc, secondaryFont);
                    SetTextColor(dc, dark ? RGB(161, 161, 166) : RGB(126, 131, 135));
                    DrawTextW(dc, readingList[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SelectObject(dc, font);
                    SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
                }
            } else {
                auto sorted = visits;
                std::sort(sorted.begin(), sorted.end(), [](const VisitEntry& a, const VisitEntry& b) { return a.lastVisited > b.lastVisited; });
                for (size_t i = 0; i < std::min(visibleRows, sorted.size()); ++i) {
                    int y = rowTop + static_cast<int>(i) * rowHeight;
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 2), width - scaleForDpi(window, 16), y + scaleForDpi(window, 20) };
                    DrawTextW(dc, sorted[i].title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT ur { scaleForDpi(window, 20), y + scaleForDpi(window, 22), width - scaleForDpi(window, 16), y + scaleForDpi(window, 39) };
                    SelectObject(dc, secondaryFont);
                    SetTextColor(dc, dark ? RGB(161, 161, 166) : RGB(126, 131, 135));
                    DrawTextW(dc, sorted[i].url.c_str(), -1, &ur, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SelectObject(dc, font);
                    SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
                }
            }
        }

        RECT footer { 0, settingsRect.top - scaleForDpi(window, 8), width, client.bottom };
        HBRUSH footerBrush = CreateSolidBrush(dark ? RGB(35, 35, 37) : RGB(247, 248, 249));
        FillRect(dc, &footer, footerBrush);
        DeleteObject(footerBrush);

        HPEN footerDivider = CreatePen(PS_SOLID, 1, dark ? RGB(62, 62, 65) : RGB(220, 223, 225));
        HPEN oldFooterPen = static_cast<HPEN>(SelectObject(dc, footerDivider));
        MoveToEx(dc, 0, footer.top, nullptr);
        LineTo(dc, width, footer.top);
        SelectObject(dc, oldFooterPen);
        DeleteObject(footerDivider);

        RECT gearRect { settingsRect.left + scaleForDpi(window, 8), settingsRect.top + scaleForDpi(window, 8),
            settingsRect.left + scaleForDpi(window, 28), settingsRect.bottom - scaleForDpi(window, 4) };
        drawSettingsGear(dc, gearRect, dark, dpi);
        RECT settingsLabel { gearRect.right + scaleForDpi(window, 8), settingsRect.top,
            settingsRect.right - scaleForDpi(window, 4), settingsRect.bottom };
        SetTextColor(dc, dark ? RGB(232, 232, 235) : RGB(42, 46, 49));
        DrawTextW(dc, L"Settings", -1, &settingsLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, oldFont);
        if (secondaryFont) DeleteObject(secondaryFont);
        if (headingFont) DeleteObject(headingFont);
        DeleteObject(font);
    }

    bool handleSidebarHit(POINT point)
    {
        if (!sidebarOpen)
            return false;

        int width = scaleForDpi(window, 286);
        int top = contentTop();
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

        const RECT settingsRect = sidebarSettingsRect();
        if (PtInRect(&settingsRect, point)) {
            openBrowserSettings(this);
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

        int rowTop = top + scaleForDpi(window, 88);
        int rowHeight = scaleForDpi(window, 42);
        const int listBottom = settingsRect.top - scaleForDpi(window, 8);
        const size_t visibleRows = listBottom > rowTop
            ? std::min<size_t>(10, static_cast<size_t>((listBottom - rowTop) / rowHeight))
            : 0;
        int index = (point.y - rowTop) / rowHeight;
        if (index < 0 || static_cast<size_t>(index) >= visibleRows)
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
        if (webFullscreen) {
            HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(dc, &client, black);
            DeleteObject(black);
            return;
        }

        int dpi = windowDpi(window);
        int toolbarH = MulDiv(kToolbarHeight, dpi, 96);
        int gap = MulDiv(kToolbarGap, dpi, 96);
        int pad = MulDiv(kToolbarHorizontalPadding, dpi, 96);

        const bool dark = chromeDarkTheme();
        SetBkMode(dc, TRANSPARENT);
        HBRUSH pageBrush = CreateSolidBrush(dark ? RGB(28, 28, 30) : RGB(250, 250, 251));
        FillRect(dc, &client, pageBrush);
        DeleteObject(pageBrush);

        HBRUSH toolbarBrush = CreateSolidBrush(dark ? RGB(47, 47, 50) : RGB(250, 250, 251));
        RECT toolbarRect { 0, 0, client.right, toolbarH };
        FillRect(dc, &toolbarRect, toolbarBrush);
        DeleteObject(toolbarBrush);

        HPEN toolbarDivider = CreatePen(PS_SOLID, 1, dark ? RGB(65, 65, 69) : RGB(226, 228, 231));
        HPEN oldToolbarDivider = static_cast<HPEN>(SelectObject(dc, toolbarDivider));
        MoveToEx(dc, 0, toolbarH - 1, nullptr);
        LineTo(dc, client.right, toolbarH - 1);
        SelectObject(dc, oldToolbarDivider);
        DeleteObject(toolbarDivider);

        drawSidebarPanel(dc);

        const int light = MulDiv(kTrafficLightSize, dpi, 96);
        const int lightGap = MulDiv(kTrafficLightGap, dpi, 96);
        const int toolbarTop = titleBarHeight();
        const int tabsTop = toolbarTop + toolbarH;
        const int tabsH = tabsBarHeight();

        const int lightY = toolbarTop + (toolbarH - light) / 2;
        drawTrafficLight(dc, pad, lightY, light, RGB(255, 95, 87));
        drawTrafficLight(dc, pad + light + lightGap, lightY, light, RGB(255, 189, 46));
        drawTrafficLight(dc, pad + (light + lightGap) * 2, lightY, light, RGB(39, 201, 63));

        RECT sidebarRect = sidebarButtonRect();
        drawRoundedControl(dc, sidebarRect,
            dark ? RGB(59, 59, 63) : RGB(251, 251, 252),
            dark ? RGB(82, 82, 86) : RGB(211, 214, 218),
            static_cast<float>(sidebarRect.bottom - sidebarRect.top) / 2.0f);

        const int sidebarIconWidth = MulDiv(22, dpi, 96);
        const int sidebarIconHeight = MulDiv(20, dpi, 96);
        RECT sidebarGlyph {
            sidebarRect.left + MulDiv(10, dpi, 96),
            (sidebarRect.top + sidebarRect.bottom - sidebarIconHeight) / 2,
            sidebarRect.left + MulDiv(10, dpi, 96) + sidebarIconWidth,
            (sidebarRect.top + sidebarRect.bottom + sidebarIconHeight) / 2
        };
        drawSidebar(dc, sidebarGlyph, dark);
        HPEN dropdownPen = CreatePen(PS_SOLID, 1, dark ? RGB(210, 210, 215) : RGB(72, 76, 80));
        HPEN oldDropdownPen = static_cast<HPEN>(SelectObject(dc, dropdownPen));
        const int dropX = sidebarRect.right - MulDiv(7, dpi, 96);
        const int dropY = (sidebarRect.top + sidebarRect.bottom) / 2;
        MoveToEx(dc, dropX - MulDiv(3, dpi, 96), dropY - MulDiv(1, dpi, 96), nullptr);
        LineTo(dc, dropX, dropY + MulDiv(2, dpi, 96));
        LineTo(dc, dropX + MulDiv(3, dpi, 96), dropY - MulDiv(1, dpi, 96));
        SelectObject(dc, oldDropdownPen);
        DeleteObject(dropdownPen);

        RECT navigationRect = navigationPillRect();
        drawRoundedControl(dc, navigationRect,
            dark ? RGB(57, 57, 61) : RGB(249, 249, 250),
            dark ? RGB(83, 83, 87) : RGB(218, 220, 223),
            static_cast<float>(navigationRect.bottom - navigationRect.top) / 2.0f);
        RECT backRect { navigationRect.left + gap, navigationRect.top,
            navigationRect.left + (navigationRect.right - navigationRect.left) / 2, navigationRect.bottom };
        RECT forwardRect { backRect.right, navigationRect.top, navigationRect.right - gap, navigationRect.bottom };
        drawChevron(dc, backRect, false, active() && WKPageCanGoBack(WKViewGetPage(active()->view.get())), dark);
        drawChevron(dc, forwardRect, true, active() && WKPageCanGoForward(WKViewGetPage(active()->view.get())), dark);

        if (tabsH > 0) {
            HBRUSH tabsBrush = CreateSolidBrush(dark ? RGB(35, 35, 37) : RGB(242, 242, 244));
            RECT tabsRow { 0, tabsTop, client.right, tabsTop + tabsH };
            FillRect(dc, &tabsRow, tabsBrush);
            DeleteObject(tabsBrush);

            for (size_t i = 0; i < tabs.size(); ++i) {
                const RECT tabRect = tabBarItemRect(i);
                float activeWeight = i == activeTab ? 1.0f : 0.0f;
                if (tabSwitchAnimating) {
                    const float progress = tabSwitchEase();
                    if (i == activeTab)
                        activeWeight = progress;
                    else if (i == previousActiveTab)
                        activeWeight = 1.0f - progress;
                    else
                        activeWeight = 0.0f;
                }
                const COLORREF inactiveFill = dark ? RGB(47, 47, 49) : RGB(233, 234, 236);
                const COLORREF activeFill = dark ? RGB(66, 66, 69) : RGB(255, 255, 255);
                const COLORREF inactiveBorder = dark ? RGB(55, 55, 58) : RGB(229, 231, 232);
                const COLORREF activeBorder = dark ? RGB(91, 91, 95) : RGB(211, 214, 216);
                drawRoundedControl(dc, tabRect,
                    blendColor(inactiveFill, activeFill, activeWeight),
                    blendColor(inactiveBorder, activeBorder, activeWeight),
                    static_cast<float>(tabRect.bottom - tabRect.top) / 2.0f);
                const int iconSize = MulDiv(16, dpi, 96);
                const int iconTop = tabRect.top + (tabRect.bottom - tabRect.top - iconSize) / 2;
                RECT iconRect { tabRect.left + MulDiv(9, dpi, 96), iconTop,
                    tabRect.left + MulDiv(9, dpi, 96) + iconSize, iconTop + iconSize };
                if (tabs[i]->favicon)
                    DrawIconEx(dc, iconRect.left, iconRect.top, tabs[i]->favicon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                else
                    drawCompass(dc, iconRect);
                RECT titleText = tabRect;
                titleText.left = iconRect.right + MulDiv(6, dpi, 96);
                titleText.right -= MulDiv(31, dpi, 96);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, dark ? RGB(240, 240, 242) : RGB(45, 49, 52));
                // Use Aurora's explicit 12px UI font for tab labels. Without selecting
                // a font here, the paint DC falls back to a larger system default font.
                HGDIOBJ oldTabFont = SelectObject(dc, uiFont);
                DrawTextW(dc, tabs[i]->title.c_str(), -1, &titleText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (oldTabFont)
                    SelectObject(dc, oldTabFont);
                RECT closeRect { tabRect.right - MulDiv(26, dpi, 96), tabRect.top, tabRect.right - MulDiv(2, dpi, 96), tabRect.bottom };
                drawClose(dc, closeRect, dark);
            }
        }

        RECT pill = addressPillRect();
        drawRoundedControl(dc, pill,
            dark ? RGB(63, 63, 67) : RGB(247, 248, 250),
            dark ? RGB(88, 88, 92) : RGB(226, 229, 233),
            static_cast<float>(pill.bottom - pill.top) / 2.0f);
        if (!addressEditing) {
            const std::wstring addressText = addressDisplayText();
            if (!addressText.empty()) {
                RECT labelRect {
                    pill.left + MulDiv(32, dpi, 96), pill.top,
                    pill.right - MulDiv(32, dpi, 96), pill.bottom
                };
                HFONT oldAddressFont = static_cast<HFONT>(SelectObject(dc, uiFont));
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, dark ? RGB(225, 225, 229) : RGB(73, 77, 81));
                DrawTextW(dc, addressText.c_str(), -1, &labelRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(dc, oldAddressFont);
            }
        }

        RECT siteSettingsGlyph { pill.left + MulDiv(2, dpi, 96), pill.top, pill.left + MulDiv(30, dpi, 96), pill.bottom };
        drawWebsiteSettings(dc, siteSettingsGlyph, dark);
        const bool loading = active() && WKPageGetEstimatedProgress(WKViewGetPage(active()->view.get())) < 1.0;
        RECT reloadRect { pill.right - MulDiv(30, dpi, 96), pill.top, pill.right, pill.bottom };
        drawReload(dc, reloadRect, loading, dark);

        RECT shareRect = rightToolbarButtonRect(0);
        RECT downloadRect = rightToolbarButtonRect(1);
        RECT newTabRect = rightToolbarButtonRect(2);
        RECT overviewRect = rightToolbarButtonRect(3);

        // Safari groups its trailing toolbar actions into a quiet rounded capsule.
        const int groupInset = MulDiv(4, dpi, 96);
        RECT actionGroup { shareRect.left - groupInset, shareRect.top,
            overviewRect.right + groupInset, overviewRect.bottom };
        const COLORREF groupFill = dark ? RGB(57, 57, 61) : RGB(255, 255, 255);
        const COLORREF groupBorder = dark ? RGB(83, 83, 87) : RGB(220, 223, 227);
        drawRoundedControl(dc, actionGroup, groupFill, groupBorder,
            static_cast<float>(actionGroup.bottom - actionGroup.top) / 2.0f);
        const COLORREF hoverFill = dark ? RGB(78, 78, 83) : RGB(238, 240, 243);
        const COLORREF hoverBorder = dark ? RGB(94, 94, 99) : RGB(209, 213, 218);
        for (int i = 0; i < 4; ++i) {
            const RECT button = rightToolbarButtonRect(i);
            if (button.right <= button.left || toolbarHoverOpacity[i] < 0.025f)
                continue;
            RECT hoverRect { button.left + MulDiv(2, dpi, 96), button.top + MulDiv(2, dpi, 96),
                button.right - MulDiv(2, dpi, 96), button.bottom - MulDiv(2, dpi, 96) };
            drawRoundedControl(dc, hoverRect,
                blendColor(groupFill, hoverFill, toolbarHoverOpacity[i]),
                blendColor(groupBorder, hoverBorder, toolbarHoverOpacity[i]),
                static_cast<float>(MulDiv(7, dpi, 96)));
        }
        drawShare(dc, shareRect, dark);
        if (downloadsButtonVisible())
            drawDownload(dc, downloadRect, dark);
        drawPlus(dc, newTabRect, dark);
        drawTabOverview(dc, overviewRect, dark);

        if (tabOverviewVisible)
            drawTabOverviewPage(dc);
    }
};

void browserWillEnterFullScreen(WKPageRef, WKCompletionListenerRef listener, const void* clientInfo)
{
    auto* browser = static_cast<BrowserState*>(const_cast<void*>(clientInfo));
    if (browser)
        browser->enterWebFullscreen();
    WKCompletionListenerComplete(listener, nullptr);
}

void browserBeganEnterFullScreen(WKPageRef, WKRect, WKRect, const void*)
{
}

void browserExitFullScreen(WKPageRef, const void* clientInfo)
{
    auto* browser = static_cast<BrowserState*>(const_cast<void*>(clientInfo));
    if (browser)
        browser->exitWebFullscreen();
}

void browserBeganExitFullScreen(WKPageRef, WKRect, WKRect,
    WKCompletionListenerRef listener, const void* clientInfo)
{
    auto* browser = static_cast<BrowserState*>(const_cast<void*>(clientInfo));
    if (browser)
        browser->exitWebFullscreen();
    WKCompletionListenerComplete(listener, nullptr);
}

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
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
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

const wchar_t* const kSettingsPageTitles[kSettingsPageCount] = {
    L"General", L"Appearance", L"Search", L"Websites", L"Privacy", L"Advanced"
};
const wchar_t* const kSettingsPageDescriptions[kSettingsPageCount] = {
    L"Choose how Aurora starts and where downloaded files are saved.",
    L"Choose the appearance used by Aurora's browser interface.",
    L"Choose the search engine used by the Smart Search field.",
    L"Set the default permissions used by websites.",
    L"Manage local browsing history and website data.",
    L"Configure advanced browser and developer options."
};
const wchar_t* const kSettingsPageIcons[kSettingsPageCount] = {
    L"\xE80F", L"\xE706", L"\xE721", L"\xE774", L"\xE72E", L"\xE943"
};

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
    HWND navigationButtons[kSettingsPageCount] { };
    std::vector<HWND> pageControls[kSettingsPageCount];
    int activePage { 0 };
};

void selectSettingsPage(HWND window, SettingsDialogState* dialog, int page)
{
    if (!dialog || page < 0 || page >= kSettingsPageCount)
        return;
    dialog->activePage = page;
    for (int i = 0; i < kSettingsPageCount; ++i) {
        const int show = i == page ? SW_SHOW : SW_HIDE;
        for (HWND control : dialog->pageControls[i]) {
            if (control)
                ShowWindow(control, show);
        }
        if (dialog->navigationButtons[i])
            InvalidateRect(dialog->navigationButtons[i], nullptr, TRUE);
    }
    InvalidateRect(window, nullptr, TRUE);
}

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
        const auto registerControl = [&](int page, HWND control) {
            if (control && page >= 0 && page < kSettingsPageCount)
                dialog->pageControls[page].push_back(control);
            return control;
        };
        auto label = [&](int page, const wchar_t* text, int x, int y, int width = 570, int height = 24) {
            HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                x, y, width, height, window, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return registerControl(page, control);
        };
        auto combo = [&](int page, int id, int x, int y, int width) {
            HWND control = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                x, y, width, 180, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return registerControl(page, control);
        };
        auto edit = [&](int page, int id, int x, int y, int width, const std::wstring& value) {
            HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                x, y, width, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return registerControl(page, control);
        };
        auto check = [&](int page, const wchar_t* text, int id, int x, int y, int width, bool checked) {
            HWND control = CreateWindowExW(0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                x, y, width, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
            return registerControl(page, control);
        };
        auto actionButton = [&](int page, const wchar_t* text, int id, int x, int y, int width) {
            HWND control = CreateWindowExW(0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                x, y, width, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return registerControl(page, control);
        };

        for (int i = 0; i < kSettingsPageCount; ++i) {
            HWND button = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                12 + i * 141, 10, 132, 64, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsNavBase + i)),
                GetModuleHandleW(nullptr), nullptr);
            if (font) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            dialog->navigationButtons[i] = button;
        }

        // General
        label(0, L"Homepage", 32, 174, 790, 22);
        dialog->homeEdit = edit(0, kSettingsHomePageEdit, 32, 202, 790, browser->homePage);
        label(0, L"Downloads location", 32, 260, 790, 22);
        const std::wstring downloads = browser->effectiveDownloadsDirectory();
        dialog->downloadsEdit = edit(0, kSettingsDownloadsEdit, 32, 288, 620, downloads);
        actionButton(0, L"Choose…", kSettingsBrowse, 666, 287, 156);
        label(0, L"Downloaded files are saved to this folder.", 32, 328, 790, 22);

        // Appearance
        label(1, L"Browser appearance", 32, 174, 790, 22);
        dialog->themeCombo = combo(1, kSettingsThemeCombo, 32, 202, 440);
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic (follow Windows)"));
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Light"));
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dark"));
        SendMessageW(dialog->themeCombo, CB_SETCURSEL,
            browser->themeMode == ThemeMode::System ? 0 : (browser->themeMode == ThemeMode::Light ? 1 : 2), 0);
        label(1, L"Choose the appearance for browser chrome and built-in pages.", 32, 242, 790, 36);

        // Search
        label(2, L"Default search engine", 32, 174, 790, 22);
        dialog->searchCombo = combo(2, kSettingsSearchCombo, 32, 202, 440);
        SendMessageW(dialog->searchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Google"));
        SendMessageW(dialog->searchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Bing"));
        SendMessageW(dialog->searchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DuckDuckGo"));
        const int searchIndex = browser->searchEngine == L"Bing" ? 1 : (browser->searchEngine == L"DuckDuckGo" ? 2 : 0);
        SendMessageW(dialog->searchCombo, CB_SETCURSEL, searchIndex, 0);
        label(2, L"Used when you type search terms in Smart Search.", 32, 242, 790, 36);

        // Websites
        label(3, L"Default website permissions", 32, 172, 790, 24);
        dialog->javascriptCheck = check(3, L"Enable JavaScript", kSettingsJavaScript, 32, 210, 310, browser->javascriptEnabled);
        dialog->imagesCheck = check(3, L"Load images automatically", kSettingsImages, 32, 248, 310, browser->imagesEnabled);
        dialog->popupsCheck = check(3, L"Allow pop-up windows", kSettingsPopups, 370, 210, 300, browser->allowPopups);
        dialog->localStorageCheck = check(3, L"Allow website storage", kSettingsLocalStorage, 370, 248, 300, browser->localStorageEnabled);
        label(3, L"These settings are used as the default for new websites.", 32, 292, 790, 36);

        // Privacy
        label(4, L"Browsing data", 32, 174, 790, 22);
        label(4, L"Clear saved history and local website data such as cookies and storage.", 32, 204, 790, 42);
        actionButton(4, L"Clear History…", kSettingsClearHistory, 32, 258, 160);
        actionButton(4, L"Clear Website Data…", kSettingsClearWebsiteData, 206, 258, 205);

        // Advanced
        dialog->developerToolsCheck = check(5, L"Enable Web Inspector / developer tools",
            kSettingsDeveloperTools, 32, 184, 410, browser->developerToolsEnabled);
        label(5, L"Some developer tools depend on the capabilities of the pinned Windows WebKit build.",
            32, 228, 790, 42);

        HWND save = CreateWindowExW(0, L"BUTTON", L"Save",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            680, 532, 82, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsSave)),
            GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            770, 532, 72, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsCancel)),
            GetModuleHandleW(nullptr), nullptr);
        if (font) {
            SendMessageW(save, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        selectSettingsPage(window, dialog, 0);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps { };
        HDC dc = BeginPaint(window, &ps);
        RECT client { };
        GetClientRect(window, &client);
        const bool dark = dialog && dialog->browser && dialog->browser->darkTheme();
        const COLORREF background = dark ? RGB(29, 29, 31) : RGB(250, 250, 252);
        const COLORREF textColor = dark ? RGB(238, 238, 241) : RGB(37, 40, 44);
        const COLORREF mutedColor = dark ? RGB(164, 165, 170) : RGB(111, 116, 123);
        HBRUSH backgroundBrush = CreateSolidBrush(background);
        FillRect(dc, &client, backgroundBrush);
        DeleteObject(backgroundBrush);
        RECT categoryBar { 0, 0, client.right, 82 };
        HBRUSH categoryBrush = CreateSolidBrush(dark ? RGB(34, 34, 36) : RGB(247, 248, 250));
        FillRect(dc, &categoryBar, categoryBrush);
        DeleteObject(categoryBrush);

        HPEN divider = CreatePen(PS_SOLID, 1, dark ? RGB(59, 59, 62) : RGB(226, 228, 232));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, divider));
        MoveToEx(dc, 0, 82, nullptr);
        LineTo(dc, client.right, 82);
        MoveToEx(dc, 30, 164, nullptr);
        LineTo(dc, client.right - 30, 164);
        MoveToEx(dc, 30, client.bottom - 54, nullptr);
        LineTo(dc, client.right - 30, client.bottom - 54);
        SelectObject(dc, oldPen);
        DeleteObject(divider);

        const int dpi = windowDpi(window);
        HFONT titleFont = CreateFontW(-MulDiv(20, dpi, 96), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT subtitleFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, textColor);
        const int page = dialog ? dialog->activePage : 0;
        RECT pageTitle { 32, 96, client.right - 30, 126 };
        DrawTextW(dc, kSettingsPageTitles[page], -1, &pageTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, subtitleFont);
        SetTextColor(dc, mutedColor);
        RECT description { 32, 128, client.right - 30, 154 };
        DrawTextW(dc, kSettingsPageDescriptions[page], -1, &description,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(subtitleFont);
        EndPaint(window, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!item || item->CtlType != ODT_BUTTON)
            break;
        const UINT id = item->CtlID;
        if (id < kSettingsNavBase || id >= kSettingsNavBase + kSettingsPageCount)
            break;
        const int page = static_cast<int>(id - kSettingsNavBase);
        const bool dark = dialog && dialog->browser && dialog->browser->darkTheme();
        const bool selected = dialog && dialog->activePage == page;
        const COLORREF selectedColor = dark ? RGB(65, 65, 69) : RGB(225, 231, 240);
        HBRUSH buttonBrush = CreateSolidBrush(selected ? selectedColor : (dark ? RGB(38, 38, 40) : RGB(242, 243, 246)));
        FillRect(item->hDC, &item->rcItem, buttonBrush);
        DeleteObject(buttonBrush);

        HFONT iconFont = CreateFontW(-MulDiv(15, windowDpi(window), 96), 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe MDL2 Assets");
        if (iconFont) {
            HFONT oldIcon = static_cast<HFONT>(SelectObject(item->hDC, iconFont));
            RECT iconRect = item->rcItem;
            iconRect.top += 4;
            iconRect.bottom = iconRect.top + 28;
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, selected ? RGB(28, 99, 210) : (dark ? RGB(218, 218, 223) : RGB(76, 81, 87)));
            DrawTextW(item->hDC, kSettingsPageIcons[page], -1, &iconRect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(item->hDC, oldIcon);
            DeleteObject(iconFont);
        }
        HFONT textFont = CreateFontW(-MulDiv(12, windowDpi(window), 96), 0, 0, 0,
            selected ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldText = static_cast<HFONT>(SelectObject(item->hDC, textFont));
        RECT labelRect = item->rcItem;
        labelRect.top += 32;
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, selected ? RGB(28, 99, 210) : (dark ? RGB(238, 238, 241) : RGB(42, 45, 49)));
        DrawTextW(item->hDC, kSettingsPageTitles[page], -1, &labelRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(item->hDC, oldText);
        DeleteObject(textFont);
        if (item->itemState & ODS_FOCUS)
            DrawFocusRect(item->hDC, &item->rcItem);
        return TRUE;
    }

    case WM_COMMAND:
        if (!dialog || !dialog->browser)
            return 0;
        if (LOWORD(wParam) >= kSettingsNavBase
            && LOWORD(wParam) < kSettingsNavBase + kSettingsPageCount) {
            selectSettingsPage(window, dialog, static_cast<int>(LOWORD(wParam) - kSettingsNavBase));
            return 0;
        }
        if (LOWORD(wParam) == kSettingsBrowse) {
            BROWSEINFOW info { };
            info.hwndOwner = window;
            info.lpszTitle = L"Choose a folder for downloads";
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
            return 0;
        }
        if (LOWORD(wParam) == kSettingsClearWebsiteData) {
            if (MessageBoxW(window, L"Clear cookies, cached site data, and local website storage for this browser window?", L"Clear Website Data", MB_YESNO | MB_ICONWARNING) != IDYES)
                return 0;
            auto* context = new ClearWebsiteDataContext {
                dialog->browser->window,
                dialog->browser->websiteDataStore.get(),
                { }
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

            const int homeLength = GetWindowTextLengthW(dialog->homeEdit);
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

            const int folderLength = GetWindowTextLengthW(dialog->downloadsEdit);
            std::wstring folder(static_cast<size_t>(folderLength) + 1, L'\0');
            GetWindowTextW(dialog->downloadsEdit, folder.data(), folderLength + 1);
            folder.resize(static_cast<size_t>(folderLength));
            while (!folder.empty() && iswspace(folder.front())) folder.erase(folder.begin());
            while (!folder.empty() && iswspace(folder.back())) folder.pop_back();
            if (!folder.empty()) {
                const DWORD attributes = GetFileAttributesW(folder.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    MessageBoxW(window, L"That downloads folder does not exist. Choose an existing folder or leave it blank to use the Windows Downloads folder.", L"Settings", MB_OK | MB_ICONWARNING);
                    selectSettingsPage(window, dialog, 0);
                    SetFocus(dialog->downloadsEdit);
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
    case WM_CTLCOLORLISTBOX:
        if (dialog && dialog->browser) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const bool dark = dialog->browser->darkTheme();
            const bool isStatic = message == WM_CTLCOLORSTATIC;
            SetTextColor(dc, dark ? RGB(238, 238, 241) : RGB(42, 45, 49));
            SetBkColor(dc, dark ? RGB(39, 39, 42) : RGB(250, 250, 252));
            SetBkMode(dc, isStatic ? TRANSPARENT : OPAQUE);
            static HBRUSH darkBrush = CreateSolidBrush(RGB(39, 39, 42));
            static HBRUSH lightBrush = CreateSolidBrush(RGB(250, 250, 252));
            return reinterpret_cast<LRESULT>(dark ? darkBrush : lightBrush);
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


void openBrowserSettings(BrowserState* browser, int requestedSection)
{
    if (!browser || !browser->window)
        return;

    for (size_t i = 0; i < browser->tabs.size(); ++i) {
        if (browser->tabs[i]->internalPage == L"settings") {
            browser->activateTab(i);
            if (auto* tab = browser->active())
                browser->loadSettingsPage(*tab, requestedSection);
            return;
        }
    }

    if (browser->addTab(true)) {
        if (auto* tab = browser->active())
            browser->loadSettingsPage(*tab, requestedSection);
    }
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

LRESULT CALLBACK addressSuggestionsProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* browser = reinterpret_cast<BrowserState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* createInfo = reinterpret_cast<CREATESTRUCTW*>(lParam);
        browser = static_cast<BrowserState*>(createInfo->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(browser));
    }

    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (browser && !browser->addressSuggestionEntries.empty()) {
            RECT client { };
            GetClientRect(window, &client);
            const int rowHeight = MulDiv(50, windowDpi(browser->window), 96);
            const int row = GET_Y_LPARAM(lParam) / std::max(1, rowHeight);
            if (row >= 0 && row < static_cast<int>(browser->addressSuggestionEntries.size())
                && browser->selectedAddressSuggestion != row) {
                browser->selectedAddressSuggestion = row;
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (browser) {
            const int rowHeight = MulDiv(50, windowDpi(browser->window), 96);
            const int row = GET_Y_LPARAM(lParam) / std::max(1, rowHeight);
            if (row >= 0 && row < static_cast<int>(browser->addressSuggestionEntries.size()))
                browser->chooseAddressSuggestion(static_cast<size_t>(row));
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps { };
        HDC dc = BeginPaint(window, &ps);
        RECT client { };
        GetClientRect(window, &client);
        const bool dark = browser && browser->chromeDarkTheme();
        const COLORREF background = dark ? RGB(39, 39, 42) : RGB(255, 255, 255);
        const COLORREF border = dark ? RGB(75, 75, 79) : RGB(219, 222, 226);
        const COLORREF primary = dark ? RGB(241, 241, 244) : RGB(34, 37, 41);
        const COLORREF secondary = dark ? RGB(164, 164, 170) : RGB(112, 117, 123);
        HBRUSH backgroundBrush = CreateSolidBrush(background);
        FillRect(dc, &client, backgroundBrush);
        DeleteObject(backgroundBrush);
        HPEN borderPen = CreatePen(PS_SOLID, 1, border);
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen));
        Rectangle(dc, client.left, client.top, client.right, client.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);
        SetBkMode(dc, TRANSPARENT);

        const int dpi = browser ? windowDpi(browser->window) : 96;
        HFONT titleFont = CreateFontW(-MulDiv(13, dpi, 96), 0, 0, 0, FW_MEDIUM,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT urlFont = CreateFontW(-MulDiv(10, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        const int rowHeight = MulDiv(50, dpi, 96);
        if (browser) {
            for (size_t i = 0; i < browser->addressSuggestionEntries.size(); ++i) {
                const int top = static_cast<int>(i) * rowHeight;
                RECT row { client.left + 1, top, client.right - 1,
                    std::min(static_cast<int>(client.bottom) - 1, top + rowHeight) };
                if (static_cast<int>(i) == browser->selectedAddressSuggestion) {
                    HBRUSH selectedBrush = CreateSolidBrush(dark ? RGB(62, 62, 66) : RGB(235, 240, 247));
                    FillRect(dc, &row, selectedBrush);
                    DeleteObject(selectedBrush);
                }
                if (i > 0) {
                    HPEN separatorPen = CreatePen(PS_SOLID, 1, border);
                    HPEN priorPen = static_cast<HPEN>(SelectObject(dc, separatorPen));
                    MoveToEx(dc, row.left + MulDiv(44, dpi, 96), row.top, nullptr);
                    LineTo(dc, row.right - MulDiv(12, dpi, 96), row.top);
                    SelectObject(dc, priorPen);
                    DeleteObject(separatorPen);
                }

                // A simple outlined site glyph keeps suggestions native to Aurora.
                const int iconSize = MulDiv(22, dpi, 96);
                const int iconLeft = row.left + MulDiv(12, dpi, 96);
                const int iconTop = row.top + (rowHeight - iconSize) / 2;
                HPEN iconPen = CreatePen(PS_SOLID, 1, secondary);
                HPEN priorIconPen = static_cast<HPEN>(SelectObject(dc, iconPen));
                HBRUSH priorIconBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
                Ellipse(dc, iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize);
                MoveToEx(dc, iconLeft + iconSize / 2, iconTop + 1, nullptr);
                LineTo(dc, iconLeft + iconSize / 2, iconTop + iconSize - 1);
                Arc(dc, iconLeft + iconSize / 4, iconTop + 1, iconLeft + iconSize * 3 / 4,
                    iconTop + iconSize - 1, iconLeft + iconSize / 2, iconTop + 1,
                    iconLeft + iconSize / 2, iconTop + iconSize - 1);
                SelectObject(dc, priorIconBrush);
                SelectObject(dc, priorIconPen);
                DeleteObject(iconPen);

                const auto& entry = browser->addressSuggestionEntries[i];
                const std::wstring title = entry.title.empty() ? entry.url : entry.title;
                RECT titleRect { iconLeft + iconSize + MulDiv(12, dpi, 96), row.top + MulDiv(6, dpi, 96),
                    row.right - MulDiv(12, dpi, 96), row.top + MulDiv(27, dpi, 96) };
                SelectObject(dc, titleFont);
                SetTextColor(dc, primary);
                DrawTextW(dc, title.c_str(), -1, &titleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                RECT urlRect { titleRect.left, row.top + MulDiv(26, dpi, 96),
                    row.right - MulDiv(12, dpi, 96), row.top + MulDiv(45, dpi, 96) };
                SelectObject(dc, urlFont);
                SetTextColor(dc, secondary);
                DrawTextW(dc, entry.url.c_str(), -1, &urlRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }

        if (titleFont) DeleteObject(titleFont);
        if (urlFont) DeleteObject(urlFont);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
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

    if (browser && message == WM_KEYDOWN) {
        if (wParam == VK_DOWN || wParam == VK_UP) {
            browser->moveAddressSuggestionSelection(wParam == VK_DOWN ? 1 : -1);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            if (browser->addressSuggestionWindow && IsWindowVisible(browser->addressSuggestionWindow)) {
                browser->hideAddressSuggestions();
            } else {
                browser->endAddressEditing();
            }
            return 0;
        }
        if (wParam == VK_RETURN) {
            if (browser->addressSuggestionWindow && IsWindowVisible(browser->addressSuggestionWindow)
                && browser->selectedAddressSuggestion >= 0) {
                browser->chooseAddressSuggestion(static_cast<size_t>(browser->selectedAddressSuggestion));
                return 0;
            }
            wchar_t text[4096] { };
            GetWindowTextW(window, text, static_cast<int>(std::size(text)));
            browser->navigateInput(text);
            browser->endAddressEditing();
            return 0;
        }
    }

    if (browser && message == WM_KILLFOCUS && browser->addressEditing) {
        LRESULT result = CallWindowProcW(browser->addressBarOriginalProcedure, window, message, wParam, lParam);
        browser->endAddressEditing();
        return result;
    }

    if (browser && message == WM_SETFOCUS) {
        LRESULT result = CallWindowProcW(browser->addressBarOriginalProcedure, window, message, wParam, lParam);
        SendMessageW(window, EM_SETSEL, 0, -1);
        if (browser->addressEditing)
            browser->updateAddressSuggestions(L"");
        return result;
    }

    if (browser && browser->addressBarOriginalProcedure) {
        LRESULT result = CallWindowProcW(browser->addressBarOriginalProcedure, window, message, wParam, lParam);
        const bool textChanged = message == WM_CHAR || message == WM_PASTE
            || message == WM_CUT || message == WM_CLEAR || message == WM_UNDO;
        if (textChanged && browser->addressEditing) {
            wchar_t text[4096] { };
            GetWindowTextW(window, text, static_cast<int>(std::size(text)));
            browser->updateAddressSuggestions(text);
        }
        return result;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}



void discardDownloadResumeData(WKDataRef, const void*)
{
}

void decidePolicyForNavigationAction(WKPageRef, WKNavigationActionRef navigationAction,
    WKFramePolicyListenerRef listener, WKTypeRef, const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (!tab || !tab->browser) {
        WKFramePolicyListenerUse(listener);
        return;
    }

    auto request = adoptWK(WKNavigationActionCopyRequest(navigationAction));
    auto urlRef = adoptWK(WKURLRequestCopyURL(request.get()));
    const std::wstring url = createString(urlRef.get());
    BrowserState* browser = tab->browser;
    if (url.rfind(L"aurora-download:", 0) == 0) {
        WKFramePolicyListenerIgnore(listener);
        const size_t queryStart = url.find(L'?');
        std::wstring command = url.substr(0, queryStart);
        while (!command.empty() && command.back() == L'/')
            command.pop_back();
        size_t index = browser->downloads.size();
        const size_t idPosition = url.find(L"id=", queryStart == std::wstring::npos ? 0 : queryStart);
        if (idPosition != std::wstring::npos) {
            const size_t idStart = idPosition + 3;
            const size_t idEnd = url.find_first_of(L"&#", idStart);
            const size_t end = idEnd == std::wstring::npos ? url.size() : idEnd;
            size_t parsedIndex = 0;
            bool validIndex = idStart < end;
            for (size_t pos = idStart; validIndex && pos < end; ++pos) {
                const wchar_t ch = url[pos];
                if (ch < L'0' || ch > L'9') {
                    validIndex = false;
                    break;
                }
                const size_t digit = static_cast<size_t>(ch - L'0');
                const size_t maxSize = static_cast<size_t>(-1);
                if (parsedIndex > (maxSize - digit) / 10) {
                    validIndex = false;
                    break;
                }
                parsedIndex = parsedIndex * 10 + digit;
            }
            index = validIndex ? parsedIndex : browser->downloads.size();
        }
        if (index >= browser->downloads.size())
            return;
        auto& entry = browser->downloads[index];
        if (command == L"aurora-download://folder") {
            const DWORD attributes = GetFileAttributesW(entry.path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                browser->showFileInFolder(entry.path);
            } else {
                const size_t slash = entry.path.find_last_of(L"\\/");
                const std::wstring folder = slash == std::wstring::npos
                    ? browser->effectiveDownloadsDirectory() : entry.path.substr(0, slash);
                if (!folder.empty())
                    ShellExecuteW(browser->window, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else if (command == L"aurora-download://cancel") {
            if (!entry.finished && !entry.failed && !entry.cancelled && entry.download) {
                entry.cancelled = true;
                WKDownloadCancel(entry.download, nullptr, discardDownloadResumeData);
                browser->refreshDownloadsPresentation();
            }
        }
        return;
    }
    if (url.rfind(L"aurora-settings:", 0) != 0) {
        WKFramePolicyListenerUse(listener);
        return;
    }
    WKFramePolicyListenerIgnore(listener);
    const size_t queryStart = url.find(L'?');
    std::wstring command = url.substr(0, queryStart);
    while (!command.empty() && command.back() == L'/')
        command.pop_back();
    if (command == L"aurora-settings://close") {
        if (browser->tabs.size() > 1)
            browser->closeTab(browser->activeTab);
        else
            browser->loadStartPage(*tab);
        return;
    }
    if (command == L"aurora-settings://clear-history") {
        browser->visits.clear();
        if (!browser->privateBrowsing)
            browser->saveVisitData();
        browser->loadSettingsPage(*tab);
        return;
    }
    if (command == L"aurora-settings://clear-data") {
        if (MessageBoxW(browser->window,
            L"Clear cookies, cached site data, and local website storage?",
            L"Clear Website Data", MB_YESNO | MB_ICONWARNING) == IDYES) {
            auto* context = new ClearWebsiteDataContext {
                browser->window, browser->websiteDataStore.get(), { }
            };
            WKWebsiteDataStoreClearStorage(context->store, context, websiteDataStorageCleared);
        }
        return;
    }
    if ((command != L"aurora-settings://save" && command != L"aurora-settings://navigate")
        || queryStart == std::wstring::npos) {
        browser->loadSettingsPage(*tab);
        return;
    }

    auto decodeComponent = [](const std::wstring& input) {
        std::string bytes;
        bytes.reserve(input.size());
        auto hexValue = [](wchar_t ch) -> int {
            if (ch >= L'0' && ch <= L'9') return static_cast<int>(ch - L'0');
            if (ch >= L'a' && ch <= L'f') return static_cast<int>(ch - L'a' + 10);
            if (ch >= L'A' && ch <= L'F') return static_cast<int>(ch - L'A' + 10);
            return -1;
        };
        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] == L'%' && i + 2 < input.size()) {
                const int hi = hexValue(input[i + 1]);
                const int lo = hexValue(input[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    bytes.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            bytes.push_back(input[i] == L'+' ? ' ' : static_cast<char>(input[i] & 0xff));
        }
        return fromUTF8(bytes);
    };
    std::wstring theme, engine, home, folder, javascript, images, popups, storage, developer, section;
    std::wstring query = url.substr(queryStart + 1);
    size_t position = 0;
    while (position <= query.size()) {
        const size_t amp = query.find(L'&', position);
        const std::wstring pair = query.substr(position,
            amp == std::wstring::npos ? std::wstring::npos : amp - position);
        const size_t equal = pair.find(L'=');
        const std::wstring key = decodeComponent(pair.substr(0, equal));
        const std::wstring value = equal == std::wstring::npos ? L"" : decodeComponent(pair.substr(equal + 1));
        if (key == L"theme") theme = value;
        else if (key == L"engine") engine = value;
        else if (key == L"home") home = value;
        else if (key == L"downloads") folder = value;
        else if (key == L"javascript") javascript = value;
        else if (key == L"images") images = value;
        else if (key == L"popups") popups = value;
        else if (key == L"storage") storage = value;
        else if (key == L"developer") developer = value;
        else if (key == L"section") section = value;
        if (amp == std::wstring::npos) break;
        position = amp + 1;
    }

    if (command == L"aurora-settings://navigate" && section.size() == 1
        && section[0] >= L'0' && section[0] <= L'5')
        tab->settingsSection = section[0] - L'0';

    if (theme == L"light") browser->themeMode = ThemeMode::Light;
    else if (theme == L"dark") browser->themeMode = ThemeMode::Dark;
    else browser->themeMode = ThemeMode::System;
    if (engine == L"Bing" || engine == L"DuckDuckGo" || engine == L"Google")
        browser->searchEngine = engine;

    while (!home.empty() && iswspace(home.front())) home.erase(home.begin());
    while (!home.empty() && iswspace(home.back())) home.pop_back();
    if (home.empty())
        home = L"https://www.google.com";
    else if (home.find(L"://") == std::wstring::npos && home.rfind(L"about:", 0) != 0)
        home = L"https://" + home;
    browser->homePage = home;

    while (!folder.empty() && iswspace(folder.front())) folder.erase(folder.begin());
    while (!folder.empty() && iswspace(folder.back())) folder.pop_back();
    if (folder.empty()) {
        browser->downloadLocation.clear();
    } else {
        const DWORD attributes = GetFileAttributesW(folder.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            browser->downloadLocation = folder;
    }
    browser->javascriptEnabled = javascript == L"1";
    browser->imagesEnabled = images == L"1";
    browser->allowPopups = popups == L"1";
    browser->localStorageEnabled = storage == L"1";
    browser->developerToolsEnabled = developer == L"1";
    browser->savePreferences();
    browser->applyEnginePreferences();
    browser->applyTheme();
    if (auto* current = browser->active(); current && current->internalPage == L"settings")
        browser->loadSettingsPage(*current);
}


void decidePolicyForNavigationResponse(WKPageRef, WKNavigationResponseRef navigationResponse,
    WKFramePolicyListenerRef listener, WKTypeRef, const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (!tab || !tab->browser) {
        WKFramePolicyListenerUse(listener);
        return;
    }

    auto frameInfo = adoptWK(WKNavigationResponseCopyFrameInfo(navigationResponse));
    if (!frameInfo || !WKFrameInfoGetIsMainFrame(frameInfo.get())) {
        WKFramePolicyListenerUse(listener);
        return;
    }

    auto response = adoptWK(WKNavigationResponseCopyResponse(navigationResponse));
    if (!response) {
        WKFramePolicyListenerUse(listener);
        return;
    }

    const bool attachment = WKURLResponseIsAttachment(response.get());
    const bool canDisplay = WKNavigationResponseCanShowMIMEType(navigationResponse);
    const std::wstring mime = createString(adoptWK(WKURLResponseCopyMIMEType(response.get())).get());
    std::wstring normalizedMime = mime;
    std::transform(normalizedMime.begin(), normalizedMime.end(), normalizedMime.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });

    // GitHub release assets and many direct download links are served as octet-stream.
    // Route these and all explicit Content-Disposition attachments through WKDownload.
    const bool binaryPayload = normalizedMime == L"application/octet-stream"
        || normalizedMime == L"application/zip"
        || normalizedMime == L"application/x-7z-compressed"
        || normalizedMime == L"application/x-rar-compressed"
        || normalizedMime == L"application/x-msdownload"
        || normalizedMime == L"application/vnd.microsoft.portable-executable"
        || normalizedMime == L"application/gzip"
        || normalizedMime == L"application/x-gzip"
        || normalizedMime == L"application/x-tar"
        || normalizedMime == L"application/x-bzip2"
        || normalizedMime == L"application/x-xz";

    if (attachment || !canDisplay || binaryPayload) {
        WKFramePolicyListenerDownload(listener);
        return;
    }

    WKFramePolicyListenerUse(listener);
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
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    std::wstring filename = createString(suggestedFilename);

    if (filename.empty() || filename == L"." || filename == L"..") {
        auto responseURL = adoptWK(WKURLResponseCopyURL(response));
        if (responseURL) {
            filename = createString(adoptWK(WKURLCopyLastPathComponent(responseURL.get())).get());
            if (filename.empty())
                filename = createString(adoptWK(WKURLCopyHostName(responseURL.get())).get());
        }
    }

    const size_t lastSeparator = filename.find_last_of(L"/\\");
    if (lastSeparator != std::wstring::npos)
        filename = filename.substr(lastSeparator + 1);
    for (auto& ch : filename) {
        if (ch < 32 || ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            ch = L'_';
    }
    while (!filename.empty() && (filename.back() == L'.' || filename.back() == L' '))
        filename.pop_back();
    if (filename.empty() || filename == L"." || filename == L"..")
        filename = L"download";

    const size_t originalDot = filename.find(L'.');
    std::wstring deviceBase = filename.substr(0, originalDot);
    std::transform(deviceBase.begin(), deviceBase.end(), deviceBase.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(towupper(ch)); });
    const bool reservedDeviceName = deviceBase == L"CON" || deviceBase == L"PRN"
        || deviceBase == L"AUX" || deviceBase == L"NUL"
        || deviceBase == L"COM1" || deviceBase == L"COM2" || deviceBase == L"COM3"
        || deviceBase == L"COM4" || deviceBase == L"COM5" || deviceBase == L"COM6"
        || deviceBase == L"COM7" || deviceBase == L"COM8" || deviceBase == L"COM9"
        || deviceBase == L"LPT1" || deviceBase == L"LPT2" || deviceBase == L"LPT3"
        || deviceBase == L"LPT4" || deviceBase == L"LPT5" || deviceBase == L"LPT6"
        || deviceBase == L"LPT7" || deviceBase == L"LPT8" || deviceBase == L"LPT9";
    if (reservedDeviceName)
        filename.insert(filename.begin(), L'_');

    auto splitName = [](const std::wstring& name) {
        const size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos || dot == 0 || name.size() - dot > 20)
            return std::pair<std::wstring, std::wstring>(name, L"");
        return std::pair<std::wstring, std::wstring>(name.substr(0, dot), name.substr(dot));
    };

    std::wstring folder = browser ? browser->effectiveDownloadsDirectory() : downloadsDirectory();
    auto isDirectory = [](const std::wstring& candidate) {
        const DWORD attributes = GetFileAttributesW(candidate.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    };
    auto isWritableDirectory = [](const std::wstring& candidate) {
        const std::wstring probe = candidate + L"\\.aurora-write-test-"
            + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".tmp";
        HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        CloseHandle(file);
        return true;
    };

    if (folder.empty() || folder.size() > 180 || !isDirectory(folder) || !isWritableDirectory(folder))
        folder = downloadsDirectory();
    if (folder.empty() || folder.size() > 180 || !isDirectory(folder) || !isWritableDirectory(folder)) {
        folder = applicationDataDirectory() + L"\\Downloads";
        CreateDirectoryW(folder.c_str(), nullptr);
    }
    if (folder.empty() || !isDirectory(folder) || !isWritableDirectory(folder)) {
        if (browser)
            MessageBoxW(browser->window,
                L"Aurora could not find a writable download folder. Choose another location in Settings and retry.",
                L"Download Failed — Aurora", MB_OK | MB_ICONWARNING);
        return nullptr;
    }

    const size_t maximumFilenameLength = folder.size() < 180 ? 220 - folder.size() - 1 : 64;
    if (filename.size() > maximumFilenameLength) {
        auto parts = splitName(filename);
        const size_t keptStem = maximumFilenameLength > parts.second.size()
            ? maximumFilenameLength - parts.second.size() : maximumFilenameLength;
        filename = parts.first.substr(0, keptStem) + parts.second;
    }
    const auto parts = splitName(filename);
    const std::wstring baseName = parts.first.empty() ? L"download" : parts.first;
    const std::wstring extension = parts.first.empty() ? L"" : parts.second;

    auto pathFor = [&](const std::wstring& name) {
        const wchar_t last = folder.back();
        return folder + ((last == L'\\' || last == L'/') ? L"" : L"\\") + name;
    };
    auto isReservedPath = [&](const std::wstring& candidatePath) {
        if (GetFileAttributesW(candidatePath.c_str()) != INVALID_FILE_ATTRIBUTES)
            return true;
        if (!browser)
            return false;
        return std::any_of(browser->downloads.begin(), browser->downloads.end(),
            [&](const DownloadEntry& entry) {
                return CompareStringOrdinal(entry.path.c_str(), -1,
                    candidatePath.c_str(), -1, TRUE) == CSTR_EQUAL;
            });
    };

    std::wstring filenameCandidate;
    std::wstring path;
    for (unsigned int suffix = 1; suffix <= 10000; ++suffix) {
        filenameCandidate = suffix == 1 ? filename
            : baseName + L" (" + std::to_wstring(suffix) + L")" + extension;
        path = pathFor(filenameCandidate);
        if (path.size() < MAX_PATH - 8 && !isReservedPath(path))
            break;
        path.clear();
    }
    if (path.empty()) {
        if (browser)
            MessageBoxW(browser->window,
                L"A unique, valid destination could not be chosen. Check the Downloads folder and retry.",
                L"Download Failed — Aurora", MB_OK | MB_ICONWARNING);
        return nullptr;
    }

    if (browser) {
        browser->downloads.push_back({ filenameCandidate, path, 0, 0, false, false, download });
        if (browser->downloads.size() > 64)
            browser->downloads.erase(browser->downloads.begin());
        browser->refreshDownloadsPresentation();
    }

    const std::string utf8Path = toUTF8(path);
    return WKStringCreateWithUTF8CString(utf8Path.c_str());
}
void downloadDidWriteData(WKDownloadRef download, long long bytesWritten, long long totalBytesWritten, long long totalBytesExpectedToWrite, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return;

    for (auto it = browser->downloads.rbegin(); it != browser->downloads.rend(); ++it) {
        if (it->download == download) {
            const long long reportedTotal = std::max(0LL, totalBytesWritten);
            const long long reportedDelta = std::max(0LL, bytesWritten);
            if (reportedTotal > it->bytesWritten)
                it->bytesWritten = reportedTotal;
            else if (reportedTotal == 0 && reportedDelta > 0)
                it->bytesWritten += reportedDelta;
            it->totalBytes = totalBytesExpectedToWrite > 0 ? totalBytesExpectedToWrite : 0;
            browser->refreshDownloadsPresentation();
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

    browser->refreshDownloadsPresentation();
}


void downloadDidFailWithError(WKDownloadRef download, WKErrorRef error, WKDataRef, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return;
    std::wstring failedPath;
    bool cancelled = false;
    for (auto it = browser->downloads.rbegin(); it != browser->downloads.rend(); ++it) {
        if (it->download == download) {
            cancelled = it->cancelled;
            if (!cancelled)
                it->failed = true;
            failedPath = it->path;
            break;
        }
    }
    if (cancelled) {
        if (!failedPath.empty())
            DeleteFileW(failedPath.c_str());
        browser->refreshDownloadsPresentation();
        return;
    }
    browser->refreshDownloadsPresentation();
    std::wstringstream details;
    details << createString(adoptWK(WKErrorCopyLocalizedDescription(error)).get()) << L"\n\n";
    details << L"Error code: " << WKErrorGetErrorCode(error) << L"\n";
    details << L"Domain: " << createString(adoptWK(WKErrorCopyDomain(error)).get()) << L"\n";
    auto failingURL = adoptWK(WKErrorCopyFailingURL(error));
    if (failingURL)
        details << L"URL: " << createString(adoptWK(WKURLCopyString(failingURL.get())).get()) << L"\n";
    if (!failedPath.empty())
        details << L"Destination: " << failedPath;
    MessageBoxW(browser->window, details.str().c_str(), L"Download Failed — Aurora", MB_OK | MB_ICONWARNING);
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
    if (tab && tab->browser) {
        const RECT address = tab->browser->addressPillRect();
        InvalidateRect(tab->browser->window, &address, FALSE);
    }
}

void didChangeCanGoBack(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser) {
        const RECT navigation = tab->browser->navigationPillRect();
        InvalidateRect(tab->browser->window, &navigation, FALSE);
    }
}

void didChangeCanGoForward(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser) {
        const RECT navigation = tab->browser->navigationPillRect();
        InvalidateRect(tab->browser->window, &navigation, FALSE);
    }
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
    case WM_NCCALCSIZE:
        if (wParam)
            return 0;
        break;

    case WM_CREATE:
        if (!state || !state->initialize(window)) {
            MessageBoxW(window, L"WebKit could not initialize Aurora.", windowTitle, MB_OK | MB_ICONERROR);
            return -1;
        }
        return 0;

    case WM_MEASUREITEM: {
        if (state && lParam) {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measure->CtlType == ODT_MENU && state->isDownloadMenuCommand(measure->itemID)) {
                measure->itemWidth = static_cast<UINT>(scaleForDpi(window, 440));
                measure->itemHeight = static_cast<UINT>(scaleForDpi(window, 68));
                return TRUE;
            }
        }
        break;
    }
    case WM_DRAWITEM: {
        if (state && lParam) {
            const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (state->drawDownloadsMenuItem(*draw))
                return TRUE;
        }
        break;
    }
    case WM_TIMER:
        if (state && wParam == kDownloadsAnimationTimerId) {
            state->animateDownloadsPopup();
            return 0;
        }
        if (state && wParam == kChromeAnimationTimerId) {
            state->animateChrome();
            return 0;
        }
        break;

    case WM_MOUSEMOVE:
        if (state) {
            if (!state->mouseTrackingForChrome) {
                TRACKMOUSEEVENT tracking { };
                tracking.cbSize = sizeof(tracking);
                tracking.dwFlags = TME_LEAVE;
                tracking.hwndTrack = window;
                state->mouseTrackingForChrome = TrackMouseEvent(&tracking) != FALSE;
            }
            POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            state->updateToolbarHover(point);
        }
        break;

    case WM_MOUSELEAVE:
        if (state) {
            state->mouseTrackingForChrome = false;
            state->hoveredToolbarButton = -1;
            state->startChromeAnimationTimer();
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
            const bool addressControl = reinterpret_cast<HWND>(lParam) == state->addressBar;
            const bool dark = addressControl ? state->chromeDarkTheme() : state->darkTheme();
            SetTextColor(dc, dark ? RGB(240, 240, 242) : RGB(45, 49, 52));
            SetBkColor(dc, dark ? RGB(57, 57, 60) : RGB(244, 245, 247));
            static HBRUSH lightAddressBrush = CreateSolidBrush(RGB(244, 245, 247));
            static HBRUSH darkAddressBrush = CreateSolidBrush(RGB(57, 57, 60));
            return reinterpret_cast<LRESULT>(dark ? darkAddressBrush : lightAddressBrush);
        }
        break;

    case WM_NCHITTEST: {
        if (state && state->webFullscreen)
            return HTCLIENT;
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

        if (state) {
            const int toolbarTop = state->titleBarHeight();
            const int toolbarBottom = toolbarTop + state->toolbarHeight();
            if (point.y >= toolbarTop && point.y < toolbarBottom
                && !state->toolbarHasInteractiveHit(point))
                return HTCAPTION;
        }

        return HTCLIENT;
    }

    case WM_KEYDOWN:
        if (state && state->tabOverviewVisible && wParam == VK_ESCAPE) {
            state->activateTab(state->activeTab);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        if (!state)
            break;

        POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (state->handleTopBarHit(point))
            return 0;
        if (state->handleToolbarHit(point))
            return 0;
        if (state->handleTabOverviewHit(point))
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
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"Could not initialize smooth browser icon rendering.", windowTitle, MB_OK | MB_ICONERROR);
        return 1;
    }
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
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    BrowserState browserState;

    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        windowClassName,
        windowTitle,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
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
        Gdiplus::GdiplusShutdown(gdiplusToken);
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

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(message.wParam);
}
