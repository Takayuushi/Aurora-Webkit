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
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKPageContextMenuClient.h>
#include <WebKit/WKContextMenuItem.h>
#include <WebKit/WKContextMenuItemTypes.h>
#include <WebKit/WKHitTestResultRef.h>
#include <WebKit/WKArray.h>
#include <WebKit/WKPageFullScreenClient.h>
#include <WebKit/WKHTTPCookieStoreRef.h>
#include <WebKit/WKData.h>
#include "WKPagePrivate.h"
#include "WKPageResumeDownload.h"
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
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <cstddef>
#include <fstream>
#include <filesystem>
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
constexpr wchar_t overlayWindowClassName[] = L"AuroraOverlayWindow";
constexpr wchar_t windowTitle[] = L"Aurora";

std::wstring auroraExecutableDirectory()
{
    wchar_t executablePath[32768] { };
    const DWORD length = GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    if (!length || length >= std::size(executablePath)) return L".";
    std::wstring path(executablePath, length);
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return L".";
    path.resize(separator);
    return path;
}
std::wstring auroraResourcePath(const wchar_t* filename)
{
    return auroraExecutableDirectory() + L"\\Resources\\" + (filename ? filename : L"");
}
std::string encodeBase64(const std::vector<unsigned char>& bytes)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve(((bytes.size()+2)/3)*4);
    for (size_t i=0;i<bytes.size();i+=3) {
        const unsigned int a=bytes[i], b=i+1<bytes.size()?bytes[i+1]:0, c=i+2<bytes.size()?bytes[i+2]:0;
        const unsigned int v=(a<<16)|(b<<8)|c;
        out.push_back(alphabet[(v>>18)&63]); out.push_back(alphabet[(v>>12)&63]);
        out.push_back(i+1<bytes.size()?alphabet[(v>>6)&63]:'=');
        out.push_back(i+2<bytes.size()?alphabet[v&63]:'=');
    }
    return out;
}
std::string imageDataUrl(const wchar_t* filename, const char* mime)
{
    if (!filename || !mime) return {};
    std::ifstream stream(std::filesystem::path(auroraResourcePath(filename)), std::ios::binary);
    if (!stream) return {};
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return {};
    return std::string("data:")+mime+";base64,"+encodeBase64(bytes);
}
std::string auroraLogoDataUrl()
{
    static const std::string value=imageDataUrl(L"aurora-logo.png","image/png");
    return value;
}
HICON createAuroraIcon()
{
    const auto path=auroraResourcePath(L"aurora-logo.png");
    Gdiplus::Bitmap bitmap(path.c_str());
    if (bitmap.GetLastStatus()!=Gdiplus::Ok) return nullptr;
    HICON icon=nullptr;
    if (bitmap.GetHICON(&icon)!=Gdiplus::Ok) return nullptr;
    return icon;
}
HICON gAuroraAppIcon { nullptr };


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
constexpr UINT WM_AURORA_TRANSITION_REPAINT_READY = WM_APP + 21;
constexpr UINT_PTR kDownloadsAnimationTimerId = 0xA112;
constexpr UINT_PTR kChromeAnimationTimerId = 0xA113;
constexpr UINT_PTR kDeferredTabLoadTimerId = 0xA116;
constexpr UINT_PTR kDeferredThumbnailCaptureTimerId = 0xA117;
constexpr UINT_PTR kWindowTransitionTimerId = 0xA118;
constexpr UINT_PTR kWindowSurfaceRecoveryTimerId = 0xA119;
constexpr UINT_PTR kSidebarZOrderTimerId = 0xA11A;
constexpr ULONGLONG kWindowTransitionDurationMs = 260;
constexpr UINT_PTR kSharePopoverTimerId = 0xA114;
constexpr UINT_PTR kDownloadsPopoverTimerId = 0xA115;
constexpr WKContextMenuItemTag kAuroraOpenLinkInNewTabTag = kWKContextMenuItemBaseApplicationTag + 1;

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
constexpr UINT kSettingsBrowseBackground = 7115;
constexpr UINT kSettingsClearBackground = 7116;
constexpr UINT kSettingsRememberWindowState = 7117;
constexpr UINT kSettingsNavBase = 7120;
constexpr int kSettingsPageCount = 6;
constexpr UINT WM_AURORA_WEBSITE_DATA_CLEARED = WM_APP + 22;

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
void resumeDownloadDidStart(WKDownloadRef, const void*);
void openBrowserSettings(BrowserState*, int requestedSection = -1);
void showSharePopover(BrowserState*);
LRESULT CALLBACK sharePopoverProcedure(HWND, UINT, WPARAM, LPARAM);
void showDownloadsPopover(BrowserState*);
LRESULT CALLBACK downloadsPopoverProcedure(HWND, UINT, WPARAM, LPARAM);
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
WKPageRef createNewTabForWebKit(WKPageRef, WKPageConfigurationRef, WKNavigationActionRef, WKWindowFeaturesRef, const void*);
void getContextMenuFromProposedMenu(WKPageRef, WKArrayRef, WKArrayRef*, WKHitTestResultRef, WKTypeRef, const void*);
void customContextMenuItemSelected(WKPageRef, WKContextMenuItemRef, const void*);

void forcedPageRepaintFinished(WKErrorRef, void* context)
{
    // Only transition handoff requests pass a context. Report completion to the
    // browser window; never synchronously draw from WebKit's completion callback.
    if (!context)
        return;
    HWND window = static_cast<HWND>(context);
    if (IsWindow(window))
        PostMessageW(window, WM_AURORA_TRANSITION_REPAINT_READY, 0, 0);
}

struct FaviconResult {
    std::wstring url;
    HICON icon { nullptr };
};

std::wstring faviconURLForPage(const std::wstring& pageURL)
{
    const size_t scheme = pageURL.find(L"://");
    if (scheme == std::wstring::npos)
        return { };

    const size_t hostStart = scheme + 3;
    const size_t hostEnd = pageURL.find_first_of(L"/?#", hostStart);
    std::wstring host = pageURL.substr(hostStart,
        hostEnd == std::wstring::npos ? std::wstring::npos : hostEnd - hostStart);
    if (host.empty())
        return { };

    // Remove URL user-info if present, and strip a trailing dot from a DNS host.
    const size_t at = host.rfind(L'@');
    if (at != std::wstring::npos)
        host.erase(0, at + 1);
    if (!host.empty() && host.back() == L'.')
        host.pop_back();
    if (host.empty())
        return { };

    return L"https://" + host + L"/favicon.ico";
}

std::vector<std::wstring> faviconCandidatesForPage(const std::wstring& pageURL)
{
    const std::wstring first = faviconURLForPage(pageURL);
    if (first.empty())
        return { };

    const size_t iconSuffix = first.rfind(L"/favicon.ico");
    if (iconSuffix == std::wstring::npos)
        return { };
    const std::wstring secureOrigin = first.substr(0, iconSuffix);
    std::wstring host = secureOrigin.substr(std::wstring(L"https://").size());
    const std::wstring insecureOrigin = L"http://" + host;

    std::vector<std::wstring> candidates {
        first,
        secureOrigin + L"/favicon.png",
        secureOrigin + L"/apple-touch-icon.png",
        // Google returns a PNG even for sites that do not publish a root favicon.ico.
        L"https://www.google.com/s2/favicons?domain=" + host + L"&sz=64"
    };
    if (pageURL.rfind(L"http://", 0) == 0)
        candidates.insert(candidates.begin() + 1, insecureOrigin + L"/favicon.ico");

    return candidates;
}

HICON loadFaviconImageFile(const wchar_t* path)
{
    if (!path || !*path)
        return nullptr;

    HICON icon = static_cast<HICON>(LoadImageW(nullptr, path, IMAGE_ICON,
        32, 32, LR_LOADFROMFILE));
    if (icon)
        return icon;

    // The fallback services and many sites provide PNG rather than ICO images.
    Gdiplus::Bitmap bitmap(path, FALSE);
    if (bitmap.GetLastStatus() != Gdiplus::Ok)
        return nullptr;

    HICON converted = nullptr;
    if (bitmap.GetHICON(&converted) != Gdiplus::Ok)
        return nullptr;
    return converted;
}

struct VisitEntry {
    std::wstring url;
    std::wstring title;
    long long visits { 0 };
    long long lastVisited { 0 };
    bool searchAction { false };
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
    bool paused { false };
    bool pausePending { false };
    bool resumePending { false };
    bool resumeUnavailable { false };
    std::vector<unsigned char> resumeData;
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

void disableNativeWindowBorder(HWND window)
{
    if (!window)
        return;

    using DwmSetWindowAttributeFunction = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetWindowAttributeFunction setWindowAttribute = []() -> DwmSetWindowAttributeFunction {
        HMODULE module = LoadLibraryW(L"dwmapi.dll");
        if (!module)
            return nullptr;
        return reinterpret_cast<DwmSetWindowAttributeFunction>(
            GetProcAddress(module, "DwmSetWindowAttribute"));
    }();
    if (!setWindowAttribute)
        return;

    // DWMWA_COLOR_NONE (0xFFFFFFFE) tells DWM not to draw the system border.
    constexpr DWORD noBorder = 0xFFFFFFFEu;
    setWindowAttribute(window, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));
}
void setWindowTextIfChanged(HWND control, const wchar_t* desiredText)
{
    if (!control || !IsWindow(control))
        return;
    const std::wstring desired = desiredText ? desiredText : L"";
    const int length = GetWindowTextLengthW(control);
    if (length == static_cast<int>(desired.size())) {
        std::wstring current(static_cast<size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(control, current.data(), length + 1);
        current.resize(static_cast<size_t>(std::max(0, copied)));
        if (current == desired)
            return;
    }
    SetWindowTextW(control, desired.c_str());
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

int CALLBACK findFontFamilyCallback(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM context)
{
    auto* found = reinterpret_cast<bool*>(context);
    if (found && font && font->lfFaceName[0]) {
        *found = true;
        return 0;
    }
    return 1;
}

bool isFontFamilyInstalled(const wchar_t* family)
{
    if (!family || !*family)
        return false;
    HDC dc = GetDC(nullptr);
    if (!dc)
        return false;

    LOGFONTW requested { };
    requested.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(requested.lfFaceName, LF_FACESIZE, family, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &requested, findFontFamilyCallback,
        reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

const wchar_t* uiFontFamily()
{
    // Inter is a close, open-source SF-style alternative when available locally.
    // Prefer the Windows variable UI font otherwise; never assume Inter is installed.
    static const bool interAvailable = isFontFamilyInstalled(L"Inter");
    return interAvailable ? L"Inter" : L"Segoe UI Variable Text";
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


void drawTrafficLight(HDC dc, int x, int y, int size, COLORREF color, bool hovered = false)
{
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const float inset = std::max(0.35f, size * 0.035f);
    const Gdiplus::RectF circle(
        static_cast<float>(x) + inset, static_cast<float>(y) + inset,
        static_cast<float>(size) - inset * 2.0f, static_cast<float>(size) - inset * 2.0f);
    if (hovered) {
        const BYTE red = GetRValue(color);
        const BYTE green = GetGValue(color);
        const BYTE blue = GetBValue(color);
        // Layer translucent, anti-aliased ellipses for a restrained native-style glow.
        for (int ring = 6; ring >= 1; --ring) {
            const BYTE alpha = static_cast<BYTE>(8 + (6 - ring) * 5);
            const float spread = static_cast<float>(ring) * std::max(0.75f, size / 13.0f);
            Gdiplus::SolidBrush glow(Gdiplus::Color(alpha, red, green, blue));
            graphics.FillEllipse(&glow,
                circle.X - spread, circle.Y - spread,
                circle.Width + spread * 2.0f, circle.Height + spread * 2.0f);
        }
    }
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
LRESULT CALLBACK overlayWindowProcedure(HWND, UINT, WPARAM, LPARAM);

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
    std::wstring lastCompletedThumbnailURL;
    std::wstring lastExtensionsUrl;
    std::wstring fallbackURL;
    HICON favicon { nullptr };
    std::wstring faviconRequestURL;
    unsigned faviconAttempts { 0 };
    HBITMAP previewBitmap { nullptr };
    int previewWidth { 0 };
    int previewHeight { 0 };
    std::wstring previewURL;
    bool thumbnailCaptureNeeded { true };
    bool initialPageLoadPending { false };
    bool initialPageShouldLoadHomepage { false };

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
    bool wasMinimized { false };
    RECT restoredWindowRect { };
    bool rememberWindowState { true };
    bool persistWindowStateForThisWindow { true };
    bool lastWindowStateValid { false };
    bool lastWindowWasMaximized { false };
    RECT lastWindowRect { };
    int lastWindowWidth { 1280 };
    int lastWindowHeight { 840 };
    RECT lastNormalWindowRect { };
    bool hasLastNormalWindowRect { false };
    bool windowTransitionActive { false };
    bool windowTransitionGeometryApplied { false };
    bool windowTransitionWaitingForRepaint { false };
    bool windowTransitionRepaintReady { false };
    int windowTransitionSettleTicks { 0 };
    ULONGLONG windowTransitionRepaintWaitStartTick { 0 };
    ULONGLONG windowTransitionLastRepaintRequestTick { 0 };
    bool windowTransitionTargetMaximized { false };
    bool windowSurfaceRecoveryActive { false };
    bool windowSurfaceRecoveryRefreshScale { false };
    bool windowSurfaceRecoveryRefreshView { true };
    bool windowSurfaceRecoveryRefreshLayout { true };
    bool windowSurfaceRecoveryPreserveOverlayStack { false };
    int windowSurfaceRecoveryTicks { 0 };
    ULONGLONG windowTransitionStartTick { 0 };
    RECT windowTransitionStartRect { };
    RECT windowTransitionTargetRect { };
    HWND windowTransitionOverlay { nullptr };
    HBITMAP windowTransitionBitmap { nullptr };
    SIZE windowTransitionBitmapSize { 0, 0 };
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
    HWND downloadsPopoverWindow { nullptr };
    HWND sharePopoverWindow { nullptr };
    HWND sidebarOverlayWindow { nullptr };
    HWND tabOverviewWindow { nullptr };
    HBITMAP chromePaintBitmap { nullptr };
    SIZE chromePaintBitmapSize { 0, 0 };
    HBITMAP sidebarOverlayBitmap { nullptr };
    SIZE sidebarOverlayBitmapSize { 0, 0 };
    bool sidebarOverlayAnimationFrameReady { false };
    HBITMAP tabOverviewBitmap { nullptr };
    SIZE tabOverviewBitmapSize { 0, 0 };
    HBITMAP tabOverviewFrameBitmap { nullptr };
    SIZE tabOverviewFrameBitmapSize { 0, 0 };
    bool tabOverviewAnimationFrameReady { false };
    bool downloadsAnimationTimerActive { false };
    bool lastDownloadsButtonVisible { false };
    bool lastReloadButtonLoading { false };
    int hoveredToolbarButton { -1 };
    int hoveredLeftControl { -1 }; // Sidebar button and website settings control
    int hoveredNavigationButton { -1 }; // Back and Forward halves of the navigation capsule
    int hoveredSidebarRow { -1 };
    bool hoveredSidebarSettings { false };
    int hoveredTrafficLight { -1 };
    float toolbarHoverOpacity[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    bool mouseTrackingForChrome { false };
    bool chromeAnimationTimerActive { false };
    bool tabSwitchAnimating { false };
    size_t previousActiveTab { 0 };
    ULONGLONG tabSwitchAnimationStart { 0 };
    bool sidebarOpen { false };
    bool sidebarAnimating { false };
    float sidebarAnimationFrom { 0.0f };
    float sidebarAnimationTarget { 0.0f };
    ULONGLONG sidebarAnimationStart { 0 };
    bool tabOverviewVisible { false };
    bool tabOverviewAnimating { false };
    ULONGLONG tabOverviewAnimationStart { 0 };
    bool tabOverviewClosing { false };
    ULONGLONG tabOverviewClosingStart { 0 };
    bool tabOverviewClosingSettling { false };
    ULONGLONG tabOverviewClosingSettleStart { 0 };
    int tabOverviewClosingSettleFramesRendered { 0 };
    ULONGLONG tabOverviewTransitionDuration { 420 };
    float tabOverviewZoomFrom { 1.0f }; // 0 = card preview, 1 = full content viewport
    float tabOverviewZoomTo { 0.0f };
    int tabOverviewOpeningFramesRendered { 0 };
    int tabOverviewClosingFramesRendered { 0 };
    int tabOverviewClosingTab { -1 };
    int hoveredOverviewTab { -1 };
    bool hoveredOverviewNewTab { false };
    int sidebarSection { 0 };
    ThemeMode themeMode { ThemeMode::System };
    std::wstring homePage { L"https://www.google.com" };
    std::wstring searchEngine { L"Google" };
    std::wstring downloadLocation;
    std::wstring startPageBackground;
    std::wstring cachedStartPageBackgroundPath;
    std::string cachedStartPageBackgroundDataUrl;
    bool javascriptEnabled { true };
    bool imagesEnabled { true };
    bool allowPopups { false };
    bool localStorageEnabled { true };
    bool developerToolsEnabled { false };
    bool showStartPageFavorites { true };
    bool showStartPageFrequentlyVisited { true };
    bool showStartPageReadingList { true };
    bool openLinksInBackground { false };
    bool searchSuggestionsEnabled { true };
    bool showFullWebsiteAddress { false };
    bool newTabsOpenWithHomepage { false };
    bool newWindowsOpenWithHomepage { false };
    bool showWebsiteIconsInTabs { true };
    bool alwaysShowTabBar { true };
    bool hasCreatedInitialTab { false };
    TabState* pendingThumbnailCaptureTab { nullptr };
    unsigned pendingThumbnailCaptureAttempts { 0 };
    std::wstring pendingThumbnailCaptureURL;
    bool sidebarZOrderTimerActive { false };
    int sidebarPaintRecoveryTicks { 0 };
    bool userSizingWindow { false };

    bool darkTheme() const { return themeMode == ThemeMode::Dark || (themeMode == ThemeMode::System && windowsPrefersDarkMode()); }
    // Safari's toolbar has its own appearance; keep System mode light by default
    // so Windows dark mode does not flatten the browser chrome into a dark strip.
    bool chromeDarkTheme() const { return themeMode == ThemeMode::Dark; }

    void loadPreferences()
    {
        std::ifstream file(preferencesPath(), std::ios::binary);
        const auto parseInteger = [](const std::wstring& text) {
            wchar_t* end = nullptr;
            const long parsed = std::wcstol(text.c_str(), &end, 10);
            if (end == text.c_str() || !end || *end != L'\0')
                return 0;
            return static_cast<int>(std::clamp(parsed, -100000L, 100000L));
        };
        bool hasTabBarPreferenceVersion = false;
        std::string line;
        while (std::getline(file, line)) {
            const size_t split = line.find('\t');
            if (split == std::string::npos)
                continue;
            const std::string key = line.substr(0, split);
            const std::wstring value = fromUTF8(line.substr(split + 1));
            if (key == "home")
                homePage = value;
            else if (key == "search" && (value == L"Google" || value == L"Bing"
                || value == L"DuckDuckGo" || value == L"Yahoo" || value == L"Ecosia"))
                searchEngine = value;
            else if (key == "downloads")
                downloadLocation = value;
            else if (key == "startPageBackground")
                startPageBackground = value;
            else if (key == "showStartPageFavorites")
                showStartPageFavorites = value == L"1" || value == L"true";
            else if (key == "showStartPageFrequentlyVisited")
                showStartPageFrequentlyVisited = value == L"1" || value == L"true";
            else if (key == "showStartPageReadingList")
                showStartPageReadingList = value == L"1" || value == L"true";
            else if (key == "openLinksInBackground")
                openLinksInBackground = value == L"1" || value == L"true";
            else if (key == "searchSuggestionsEnabled")
                searchSuggestionsEnabled = value == L"1" || value == L"true";
            else if (key == "showFullWebsiteAddress")
                showFullWebsiteAddress = value == L"1" || value == L"true";
            else if (key == "newTabsOpenWithHomepage")
                newTabsOpenWithHomepage = value == L"1" || value == L"true";
            else if (key == "newWindowsOpenWithHomepage")
                newWindowsOpenWithHomepage = value == L"1" || value == L"true";
            else if (key == "showWebsiteIconsInTabs")
                showWebsiteIconsInTabs = value == L"1" || value == L"true";
            else if (key == "alwaysShowTabBar")
                alwaysShowTabBar = value == L"1" || value == L"true";
            else if (key == "alwaysShowTabBarPreferenceVersion")
                hasTabBarPreferenceVersion = value == L"1" || value == L"true";
            else if (key == "rememberWindowState")
                rememberWindowState = value == L"1" || value == L"true";
            else if (key == "lastWindowStateValid")
                lastWindowStateValid = value == L"1" || value == L"true";
            else if (key == "lastWindowWasMaximized")
                lastWindowWasMaximized = value == L"1" || value == L"true";
            else if (key == "lastWindowLeft")
                lastWindowRect.left = parseInteger(value);
            else if (key == "lastWindowTop")
                lastWindowRect.top = parseInteger(value);
            else if (key == "lastWindowWidth")
                lastWindowWidth = parseInteger(value);
            else if (key == "lastWindowHeight")
                lastWindowHeight = parseInteger(value);
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
        // Older builds persisted the default false value even when the user had
        // never changed it. Migrate those profiles to the Safari-like single-tab bar
        // once; the version marker below preserves subsequent explicit choices.
        if (!hasTabBarPreferenceVersion)
            alwaysShowTabBar = true;

        if (lastWindowStateValid) {
            if (lastWindowWidth < 780 || lastWindowWidth > 20000
                || lastWindowHeight < 520 || lastWindowHeight > 12000) {
                lastWindowStateValid = false;
            } else {
                lastWindowRect.right = lastWindowRect.left + lastWindowWidth;
                lastWindowRect.bottom = lastWindowRect.top + lastWindowHeight;
            }
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
        std::wstring safeStartPageBackground = startPageBackground;
        replaceTabsAndNewlines(safeHome);
        replaceTabsAndNewlines(safeSearch);
        replaceTabsAndNewlines(safeDownloads);
        replaceTabsAndNewlines(safeStartPageBackground);
        file << "home\t" << toUTF8(safeHome) << '\n'
             << "search\t" << toUTF8(safeSearch) << '\n'
             << "downloads\t" << toUTF8(safeDownloads) << '\n'
             << "startPageBackground\t" << toUTF8(safeStartPageBackground) << '\n'
             << "showStartPageFavorites\t" << (showStartPageFavorites ? "1" : "0") << '\n'
             << "showStartPageFrequentlyVisited\t" << (showStartPageFrequentlyVisited ? "1" : "0") << '\n'
             << "showStartPageReadingList\t" << (showStartPageReadingList ? "1" : "0") << '\n'
             << "openLinksInBackground\t" << (openLinksInBackground ? "1" : "0") << '\n'
             << "searchSuggestionsEnabled\t" << (searchSuggestionsEnabled ? "1" : "0") << '\n'
             << "showFullWebsiteAddress\t" << (showFullWebsiteAddress ? "1" : "0") << '\n'
             << "newTabsOpenWithHomepage\t" << (newTabsOpenWithHomepage ? "1" : "0") << '\n'
             << "newWindowsOpenWithHomepage\t" << (newWindowsOpenWithHomepage ? "1" : "0") << '\n'
             << "showWebsiteIconsInTabs\t" << (showWebsiteIconsInTabs ? "1" : "0") << '\n'
             << "alwaysShowTabBar\t" << (alwaysShowTabBar ? "1" : "0") << '\n'
             << "alwaysShowTabBarPreferenceVersion\t1\n"
             << "rememberWindowState\t" << (rememberWindowState ? "1" : "0") << '\n'
             << "lastWindowStateValid\t" << (lastWindowStateValid ? "1" : "0") << '\n'
             << "lastWindowWasMaximized\t" << (lastWindowWasMaximized ? "1" : "0") << '\n'
             << "lastWindowLeft\t" << lastWindowRect.left << '\n'
             << "lastWindowTop\t" << lastWindowRect.top << '\n'
             << "lastWindowWidth\t" << lastWindowWidth << '\n'
             << "lastWindowHeight\t" << lastWindowHeight << '\n'
             << "javascript\t" << (javascriptEnabled ? "1" : "0") << '\n'
             << "images\t" << (imagesEnabled ? "1" : "0") << '\n'
             << "popups\t" << (allowPopups ? "1" : "0") << '\n'
             << "localStorage\t" << (localStorageEnabled ? "1" : "0") << '\n'
             << "developerTools\t" << (developerToolsEnabled ? "1" : "0") << '\n';
    }

    bool applySavedWindowState()
    {
        if (!window || !rememberWindowState || !lastWindowStateValid)
            return false;
        RECT normal = lastWindowRect;
        HMONITOR monitor = MonitorFromRect(&normal, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info { };
        info.cbSize = sizeof(info);
        if (!monitor || !GetMonitorInfoW(monitor, &info))
            return false;
        const int workWidth = std::max(1L, info.rcWork.right - info.rcWork.left);
        const int workHeight = std::max(1L, info.rcWork.bottom - info.rcWork.top);
        const int minimumWidth = std::min(780, workWidth);
        const int minimumHeight = std::min(520, workHeight);
        const int width = std::clamp(lastWindowWidth, minimumWidth, workWidth);
        const int height = std::clamp(lastWindowHeight, minimumHeight, workHeight);
        const int visibleWidth = std::min(120, width);
        const int visibleTop = std::min(80, height);
        normal.left = static_cast<LONG>(std::clamp(static_cast<int>(normal.left),
            static_cast<int>(info.rcWork.left) - width + visibleWidth,
            static_cast<int>(info.rcWork.right) - visibleWidth));
        normal.top = static_cast<LONG>(std::clamp(static_cast<int>(normal.top),
            static_cast<int>(info.rcWork.top),
            static_cast<int>(info.rcWork.bottom) - visibleTop));
        normal.right = normal.left + width;
        normal.bottom = normal.top + height;
        const RECT target = lastWindowWasMaximized ? info.rcWork : normal;
        const int targetWidth = target.right - target.left;
        const int targetHeight = target.bottom - target.top;
        if (targetWidth < 1 || targetHeight < 1
            || !SetWindowPos(window, HWND_TOP, target.left, target.top,
                targetWidth, targetHeight,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED))
            return false;
        lastWindowRect = normal;
        lastWindowWidth = width;
        lastWindowHeight = height;
        restoredWindowRect = normal;
        lastNormalWindowRect = normal;
        hasLastNormalWindowRect = true;
        customMaximized = lastWindowWasMaximized;
        return true;
    }

    void updateLastNormalWindowRect()
    {
        if (!window || customMaximized || windowTransitionActive
            || webFullscreen || IsIconic(window))
            return;
        RECT current { };
        if (GetWindowRect(window, &current)
            && current.right > current.left && current.bottom > current.top) {
            lastNormalWindowRect = current;
            hasLastNormalWindowRect = true;
        }
    }

    void saveWindowStateOnExit()
    {
        if (!persistWindowStateForThisWindow)
            return;
        if (!rememberWindowState) {
            lastWindowStateValid = false;
            savePreferences();
            return;
        }
        RECT normal { };
        bool maximized = customMaximized;
        if (windowTransitionActive) {
            if (windowTransitionTargetMaximized) {
                normal = windowTransitionStartRect;
                maximized = true;
            } else {
                normal = windowTransitionTargetRect;
                maximized = false;
            }
        } else if (webFullscreen) {
            normal = fullscreenRestoreWindowRect;
            maximized = fullscreenRestoreWasMaximized;
            if (maximized)
                normal = restoredWindowRect;
        } else if (customMaximized) {
            normal = restoredWindowRect;
            maximized = true;
        } else if (IsIconic(window) && hasLastNormalWindowRect) {
            normal = lastNormalWindowRect;
            maximized = false;
        } else if (!GetWindowRect(window, &normal)) {
            normal = lastNormalWindowRect;
        }
        const int width = normal.right - normal.left;
        const int height = normal.bottom - normal.top;
        if (width < 1 || height < 1) {
            lastWindowStateValid = false;
            savePreferences();
            return;
        }
        lastWindowRect = normal;
        lastWindowWidth = width;
        lastWindowHeight = height;
        lastWindowWasMaximized = maximized;
        lastWindowStateValid = true;
        savePreferences();
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

    static std::string startPageImageMimeType(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos)
            return {};
        std::wstring extension = path.substr(dot);
        if (!extension.empty())
            CharLowerBuffW(extension.data(), static_cast<DWORD>(extension.size()));
        if (extension == L".png") return "image/png";
        if (extension == L".jpg" || extension == L".jpeg") return "image/jpeg";
        if (extension == L".webp") return "image/webp";
        if (extension == L".gif") return "image/gif";
        if (extension == L".bmp") return "image/bmp";
        return {};
    }

    std::string startPageBackgroundDataUrl()
    {
        if (startPageBackground.empty())
            return {};
        if (cachedStartPageBackgroundPath == startPageBackground
            && !cachedStartPageBackgroundDataUrl.empty())
            return cachedStartPageBackgroundDataUrl;

        const std::string mime = startPageImageMimeType(startPageBackground);
        if (mime.empty())
            return {};

        HANDLE imageFile = CreateFileW(startPageBackground.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (imageFile == INVALID_HANDLE_VALUE)
            return {};

        LARGE_INTEGER fileSize { };
        constexpr LONGLONG maxImageBytes = 24LL * 1024LL * 1024LL;
        if (!GetFileSizeEx(imageFile, &fileSize) || fileSize.QuadPart <= 0
            || fileSize.QuadPart > maxImageBytes) {
            CloseHandle(imageFile);
            return {};
        }

        std::vector<unsigned char> bytes(static_cast<size_t>(fileSize.QuadPart));
        size_t offset = 0;
        while (offset < bytes.size()) {
            const DWORD request = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
            DWORD received = 0;
            if (!ReadFile(imageFile, bytes.data() + offset, request, &received, nullptr) || !received) {
                CloseHandle(imageFile);
                return {};
            }
            offset += received;
        }
        CloseHandle(imageFile);

        static constexpr char encoding[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string base64;
        base64.reserve(((bytes.size() + 2) / 3) * 4);
        for (size_t i = 0; i < bytes.size(); i += 3) {
            const uint32_t first = bytes[i];
            const uint32_t second = i + 1 < bytes.size() ? bytes[i + 1] : 0;
            const uint32_t third = i + 2 < bytes.size() ? bytes[i + 2] : 0;
            const uint32_t triple = (first << 16) | (second << 8) | third;
            base64.push_back(encoding[(triple >> 18) & 0x3f]);
            base64.push_back(encoding[(triple >> 12) & 0x3f]);
            base64.push_back(i + 1 < bytes.size() ? encoding[(triple >> 6) & 0x3f] : '=');
            base64.push_back(i + 2 < bytes.size() ? encoding[triple & 0x3f] : '=');
        }

        cachedStartPageBackgroundPath = startPageBackground;
        cachedStartPageBackgroundDataUrl = "data:" + mime + ";base64," + base64;
        return cachedStartPageBackgroundDataUrl;
    }

    bool chooseStartPageBackground(HWND owner)
    {
        std::vector<wchar_t> selectedPath(32768, L'\0');
        const wchar_t filter[] =
            L"Image files (*.png;*.jpg;*.jpeg;*.webp;*.gif;*.bmp)\0*.png;*.jpg;*.jpeg;*.webp;*.gif;*.bmp\0"
            L"All files (*.*)\0*.*\0\0";
        OPENFILENAMEW dialog { };
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner ? owner : window;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = selectedPath.data();
        dialog.nMaxFile = static_cast<DWORD>(selectedPath.size());
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
            | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
        if (!GetOpenFileNameW(&dialog))
            return false;

        const std::wstring candidate = selectedPath.data();
        if (startPageImageMimeType(candidate).empty()) {
            MessageBoxW(owner ? owner : window,
                L"Choose a PNG, JPEG, WebP, GIF, or BMP image.",
                L"Start Page Background", MB_OK | MB_ICONINFORMATION);
            return false;
        }

        HANDLE imageFile = CreateFileW(candidate.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER fileSize { };
        constexpr LONGLONG maxImageBytes = 24LL * 1024LL * 1024LL;
        const bool sizeValid = imageFile != INVALID_HANDLE_VALUE
            && GetFileSizeEx(imageFile, &fileSize) && fileSize.QuadPart > 0
            && fileSize.QuadPart <= maxImageBytes;
        if (imageFile != INVALID_HANDLE_VALUE)
            CloseHandle(imageFile);
        if (!sizeValid) {
            MessageBoxW(owner ? owner : window,
                L"That image is empty or larger than 24 MB. Choose a smaller image for the Start Page background.",
                L"Start Page Background", MB_OK | MB_ICONWARNING);
            return false;
        }

        startPageBackground = candidate;
        cachedStartPageBackgroundPath.clear();
        cachedStartPageBackgroundDataUrl.clear();
        savePreferences();
        refreshOpenStartPages();
        return true;
    }

    void clearStartPageBackground()
    {
        startPageBackground.clear();
        cachedStartPageBackgroundPath.clear();
        cachedStartPageBackgroundDataUrl.clear();
        savePreferences();
        refreshOpenStartPages();
    }

    std::wstring searchURL(const std::wstring& query) const
    {
        const std::wstring encoded = percentEncode(query);
        if (searchEngine == L"Bing")
            return L"https://www.bing.com/search?q=" + encoded;
        if (searchEngine == L"DuckDuckGo")
            return L"https://duckduckgo.com/?q=" + encoded;
        if (searchEngine == L"Yahoo")
            return L"https://search.yahoo.com/search?p=" + encoded;
        if (searchEngine == L"Ecosia")
            return L"https://www.ecosia.org/search?q=" + encoded;
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
        disableNativeWindowBorder(window);
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
            else if (tab.internalPage == L"about") loadSettingsPage(tab, 12);
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
        InvalidateRect(window, nullptr, FALSE);
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
    std::wstring pendingContextMenuLinkURL;
    size_t activeTab { 0 };

    int titleBarHeight() const { return webFullscreen ? 0 : scaleForDpi(window, kTitleBarHeight); }
    int toolbarHeight() const { return webFullscreen ? 0 : scaleForDpi(window, kToolbarHeight); }
    int tabsBarHeight() const {
        return webFullscreen || tabs.empty() || (tabs.size() <= 1 && !alwaysShowTabBar)
            ? 0 : scaleForDpi(window, kTabsBarHeight);
    }
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
        InvalidateRect(window, nullptr, FALSE);
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
        InvalidateRect(window, nullptr, FALSE);
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
        const int width = scaleForDpi(window, 300);
        const int margin = scaleForDpi(window, 12);
        const int height = scaleForDpi(window, 52);
        const int bottomMargin = scaleForDpi(window, 8);
        return { margin, client.bottom - height, width - margin, client.bottom - bottomMargin };
    }

    RECT sidebarRowRect(int index) const
    {
        const int width = scaleForDpi(window, 300);
        const int rowTop = contentTop() + scaleForDpi(window, 88);
        const int rowHeight = scaleForDpi(window, 46);
        const int inset = scaleForDpi(window, 8);
        return { inset, rowTop + index * rowHeight + scaleForDpi(window, 1),
            width - inset, rowTop + (index + 1) * rowHeight - scaleForDpi(window, 1) };
    }

    void invalidateSidebarOverlayRect(const RECT& parentRect)
    {
        if (!sidebarOverlayWindow || !IsWindow(sidebarOverlayWindow)
            || !IsWindowVisible(sidebarOverlayWindow))
            return;
        RECT localRect = parentRect;
        OffsetRect(&localRect, 0, -contentTop());
        InvalidateRect(sidebarOverlayWindow, &localRect, FALSE);
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
        // Let the active tab meet the page edge like Safari; the old bottom inset
        // left a visible strip between the tab capsule and the content viewport.
        return { left, rowTop + inset, left + tabWidth, rowTop + rowHeight };
    }


    bool downloadsHaveInProgress() const
    {
        return std::any_of(downloads.begin(), downloads.end(), [](const DownloadEntry& item) {
            return !item.finished && !item.failed && !item.cancelled && !item.paused;
        });
    }

    bool downloadsButtonVisible() const
    {
        // Keep the Downloads control available until the list is cleared, even after
        // a transfer finishes or fails.
        return !downloads.empty();
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

    RECT leftToolbarControlRect(int index) const
    {
        if (index == 0)
            return sidebarButtonRect();
        if (index == 1)
            return navigationPillRect();
        if (index == 2) {
            RECT pill = addressPillRect();
            pill.right = std::min(pill.right, pill.left + scaleForDpi(window, 30));
            return pill;
        }
        return { 0, 0, 0, 0 };
    }

    void startChromeAnimationTimer()
    {
        if (!window || chromeAnimationTimerActive)
            return;
        if (SetTimer(window, kChromeAnimationTimerId, 16, nullptr))
            chromeAnimationTimerActive = true;
    }

    float sidebarProgress() const
    {
        if (!sidebarAnimating)
            return sidebarOpen ? 1.0f : 0.0f;
        const ULONGLONG elapsed = GetTickCount64() - sidebarAnimationStart;
        const float t = std::clamp(static_cast<float>(elapsed) / 240.0f, 0.0f, 1.0f);
        // Cubic ease-out starts promptly but settles gently instead of visibly
        // crawling through the final portion of the sidebar slide.
        const float inverse = 1.0f - t;
        const float eased = 1.0f - inverse * inverse * inverse;
        return sidebarAnimationFrom + (sidebarAnimationTarget - sidebarAnimationFrom) * eased;
    }

    int sidebarOffset() const
    {
        return static_cast<int>(std::lround(
            static_cast<float>(scaleForDpi(window, 300)) * sidebarProgress()));
    }

    bool ensureOverlayWindow(HWND& handle)
    {
        if (!window)
            return false;
        if (handle && IsWindow(handle))
            return true;
        handle = CreateWindowExW(0, overlayWindowClassName, L"", WS_CHILD,
            0, 0, 0, 0, window, nullptr, GetModuleHandleW(nullptr), this);
        return handle != nullptr;
    }

    bool ensureOverlayWindows()
    {
        return ensureOverlayWindow(sidebarOverlayWindow)
            && ensureOverlayWindow(tabOverviewWindow);
    }

    void positionOverlayWindows(bool forceTop = false)
    {
        if (!window || IsIconic(window))
            return;

        RECT client { };
        if (!GetClientRect(window, &client))
            return;

        const int top = contentTop();
        const int height = std::max(0, static_cast<int>(client.bottom) - top);
        // Keep the sidebar on its own HWND both while moving and while settled.
        // The WebKit view remains stationary underneath it, avoiding renderer
        // blackouts caused by moving/resizing the accelerated WKView child.
        if (sidebarOpen || sidebarAnimating) {
            if (ensureOverlayWindow(sidebarOverlayWindow)) {
                const int width = scaleForDpi(window, 300);
                const int left = sidebarOffset() - width;
                const bool alreadyVisible = IsWindowVisible(sidebarOverlayWindow) != FALSE;
                RECT previousRect { };
                bool havePreviousRect = alreadyVisible
                    && GetWindowRect(sidebarOverlayWindow, &previousRect);
                if (havePreviousRect) {
                    MapWindowPoints(nullptr, window,
                        reinterpret_cast<POINT*>(&previousRect), 2);
                }

                const bool geometryChanged = !alreadyVisible || !havePreviousRect
                    || previousRect.left != left || previousRect.top != top
                    || previousRect.right - previousRect.left != width
                    || previousRect.bottom - previousRect.top != height;

                if (!alreadyVisible) {
                    // Show the drawer once; its cached bitmap then moves without
                    // repeatedly changing sibling HWND order during the animation.
                    SetWindowPos(sidebarOverlayWindow, HWND_TOP,
                        left, top, width, height,
                        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW);
                    ShowWindow(sidebarOverlayWindow, SW_SHOWNOACTIVATE);
                    InvalidateRect(sidebarOverlayWindow, nullptr, TRUE);
                    RedrawWindow(sidebarOverlayWindow, nullptr, nullptr,
                        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
                } else {
                    // Raising and invalidating the full drawer on every tick competes
                    // with WebKit's accelerated surface and leaves stale overlay pixels.
                    if (geometryChanged || (forceTop && !sidebarAnimating)) {
                        UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
                        const bool preserveStack = sidebarAnimating || !forceTop;
                        if (preserveStack)
                            flags |= SWP_NOZORDER;
                        if (sidebarAnimating)
                            flags |= SWP_NOREDRAW;
                        if (!geometryChanged)
                            flags |= SWP_NOMOVE | SWP_NOSIZE;
                        SetWindowPos(sidebarOverlayWindow,
                            preserveStack ? nullptr : HWND_TOP,
                            left, top, width, height, flags);
                    }

                    if (!sidebarAnimating && geometryChanged)
                        InvalidateRect(sidebarOverlayWindow, nullptr, FALSE);
                    if (forceTop && !sidebarAnimating && !userSizingWindow
                        && !windowTransitionActive && !tabOverviewAnimating && !tabOverviewClosing)
                        RedrawWindow(sidebarOverlayWindow, nullptr, nullptr,
                            RDW_INVALIDATE | RDW_NOERASE);
                }

                // When the drawer closes, invalidate only the strip it uncovered.
                // Keep the request asynchronous and local so the entire WebKit surface
                // is not synchronously repainted during the sidebar animation.
                if (sidebarAnimating && geometryChanged && havePreviousRect
                    && left < previousRect.left) {
                    RECT clientRect { };
                    if (GetClientRect(window, &clientRect)) {
                        RECT exposed { left + width, top,
                            previousRect.left + width, top + height };
                        RECT clipped { };
                        if (IntersectRect(&clipped, &exposed, &clientRect)) {
                            if (tabOverviewVisible && tabOverviewWindow
                                && IsWindowVisible(tabOverviewWindow)) {
                                RECT overviewRect { };
                                if (GetWindowRect(tabOverviewWindow, &overviewRect)) {
                                    MapWindowPoints(nullptr, window,
                                        reinterpret_cast<POINT*>(&overviewRect), 2);
                                    RECT local = clipped;
                                    OffsetRect(&local, -overviewRect.left, -overviewRect.top);
                                    InvalidateRect(tabOverviewWindow, &local, FALSE);
                                }
                            } else if (!sidebarOpen) {
                                if (auto* selected = active()) {
                                    HWND viewWindow = WKViewGetWindow(selected->view.get());
                                    RECT viewRect { };
                                    if (viewWindow && IsWindow(viewWindow)
                                        && GetWindowRect(viewWindow, &viewRect)) {
                                        MapWindowPoints(nullptr, window,
                                            reinterpret_cast<POINT*>(&viewRect), 2);
                                        RECT local = clipped;
                                        OffsetRect(&local, -viewRect.left, -viewRect.top);
                                        InvalidateRect(viewWindow, &local, FALSE);
                                        RedrawWindow(viewWindow, &local, nullptr,
                                            RDW_INVALIDATE | RDW_NOERASE);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (sidebarOverlayWindow && IsWindow(sidebarOverlayWindow)
            && IsWindowVisible(sidebarOverlayWindow)) {
            ShowWindow(sidebarOverlayWindow, SW_HIDE);
        }

        if (tabOverviewVisible || tabOverviewClosing) {
            if (ensureOverlayWindow(tabOverviewWindow)) {
                // Keep the overview canvas full-width while the sidebar slides over it.
                // Shift to the settled content inset only once the drawer animation ends;
                // resizing its full-page backing bitmap on every frame would reintroduce lag.
                const int left = sidebarAnimating ? 0
                    : (sidebarOpen ? scaleForDpi(window, 300) : 0);
                const int width = std::max(0, static_cast<int>(client.right) - left);
                const bool alreadyVisible = IsWindowVisible(tabOverviewWindow) != FALSE;
                RECT currentRect { };
                bool geometryChanged = true;
                if (alreadyVisible && GetWindowRect(tabOverviewWindow, &currentRect)) {
                    MapWindowPoints(nullptr, window,
                        reinterpret_cast<POINT*>(&currentRect), 2);
                    geometryChanged = currentRect.left != left || currentRect.top != top
                        || currentRect.right - currentRect.left != width
                        || currentRect.bottom - currentRect.top != height;
                }

                // The overview is a full-page native canvas. While the drawer animates,
                // its target geometry is constant; repeatedly SetWindowPos + invalidating
                // this entire canvas makes every 16 ms drawer frame redraw every tab card.
                // Only move/paint the canvas when its actual geometry changes, or when a
                // settled layout explicitly needs its z-order restored.
                const bool freezeOverviewDuringSidebarSlide = alreadyVisible
                    && sidebarAnimating && tabOverviewVisible
                    && !tabOverviewAnimating && !tabOverviewClosing;
                const bool shouldPosition = !alreadyVisible || geometryChanged
                    || (forceTop && !freezeOverviewDuringSidebarSlide);
                if (shouldPosition) {
                    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
                    if (alreadyVisible && !forceTop)
                        flags |= SWP_NOZORDER;
                    else if (!alreadyVisible)
                        flags |= SWP_SHOWWINDOW;
                    SetWindowPos(tabOverviewWindow,
                        alreadyVisible && !forceTop ? nullptr : HWND_TOP,
                        left, top, width, height, flags);
                }
                if (!alreadyVisible || geometryChanged
                    || (forceTop && !freezeOverviewDuringSidebarSlide))
                    InvalidateRect(tabOverviewWindow, nullptr, FALSE);
            }
        } else if (tabOverviewWindow && IsWindow(tabOverviewWindow)
            && IsWindowVisible(tabOverviewWindow)) {
            ShowWindow(tabOverviewWindow, SW_HIDE);
        }

        // When both overlays are visible, keep the drawer above the overview. Apply
        // this once after forced layout passes rather than on each animation tick.
        if (forceTop && (sidebarOpen || sidebarAnimating)
            && sidebarOverlayWindow && IsWindowVisible(sidebarOverlayWindow)) {
            SetWindowPos(sidebarOverlayWindow, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                    | SWP_NOOWNERZORDER | SWP_NOREDRAW);
        }
    }

    void beginWindowSurfaceRecovery(bool refreshBackingScale = false, bool refreshWebView = true,
        bool refreshLayout = true, bool preserveOverlayStack = false)
    {
        if (!window || IsIconic(window))
            return;
        windowSurfaceRecoveryTicks = 0;
        windowSurfaceRecoveryRefreshScale = refreshBackingScale;
        windowSurfaceRecoveryRefreshView = refreshWebView;
        windowSurfaceRecoveryRefreshLayout = refreshLayout;
        windowSurfaceRecoveryPreserveOverlayStack = preserveOverlayStack;
        windowSurfaceRecoveryActive = SetTimer(
            window, kWindowSurfaceRecoveryTimerId, 48, nullptr) != 0;
        recoverWindowSurfaceFrame();
    }

    void recoverWindowSurfaceFrame()
    {
        if (!window)
            return;
        if (IsIconic(window)) {
            KillTimer(window, kWindowSurfaceRecoveryTimerId);
            windowSurfaceRecoveryActive = false;
            return;
        }

        ++windowSurfaceRecoveryTicks;
        const bool overlayAnimationInProgress = sidebarAnimating
            || tabOverviewAnimating || tabOverviewClosing;
        if (windowSurfaceRecoveryTicks == 1) {
            // Avoid a synchronous layout/redraw pass during the sidebar slide.
            if (overlayAnimationInProgress && !windowSurfaceRecoveryRefreshView)
                positionOverlayWindows(false);
            else if (windowSurfaceRecoveryRefreshLayout)
                resize(false);
            else
                positionOverlayWindows(false);
            if (windowSurfaceRecoveryRefreshScale) {
                if (auto* selected = active()) {
                    if (auto page = WKViewGetPage(selected->view.get())) {
                        const double targetScale = windowScaleFactor(window);
                        // Reapply scale only when the monitor DPI actually changed.
                        // Resetting an unchanged scale rebuilds WebKit's backing
                        // surface and can leave an enlarged page briefly blurry.
                        if (std::abs(WKPageGetBackingScaleFactor(page) - targetScale) > 0.001)
                            WKPageSetCustomBackingScaleFactor(page, targetScale);
                    }
                }
            }
        } else {
            // WebKit's accelerated child can repaint after the parent is restored;
            // reassert overlay z-order without forcing an animation frame synchronously.
            positionOverlayWindows(!overlayAnimationInProgress);
        }

        if (windowSurfaceRecoveryRefreshView) {
            if (windowSurfaceRecoveryTicks == 1
                || (windowSurfaceRecoveryTicks % 3) == 0) {
                // Re-request real WebKit frames through the settle period. A single
                // request plus a few HWND invalidations can leave the accelerated
                // surface stale until a mouse event happens to hit the page.
                requestActivePageRepaint(windowSurfaceRecoveryTicks == 1
                    && windowSurfaceRecoveryRefreshScale, false,
                    windowSurfaceRecoveryPreserveOverlayStack);
            } else if (auto* selected = active()) {
                HWND viewWindow = WKViewGetWindow(selected->view.get());
                if (viewWindow && IsWindowVisible(viewWindow)) {
                    InvalidateRect(viewWindow, nullptr, FALSE);
                    RedrawWindow(viewWindow, nullptr, nullptr,
                        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
                }
            }
        }

        // Keep browser chrome and native overlay windows above the just-raised WKView.
        positionOverlayWindows(!overlayAnimationInProgress);
        if (sidebarOpen)
            maintainSidebarZOrder();

        if (sidebarOpen || sidebarAnimating) {
            if (sidebarOverlayWindow && IsWindowVisible(sidebarOverlayWindow)) {
                InvalidateRect(sidebarOverlayWindow, nullptr, FALSE);
                RedrawWindow(sidebarOverlayWindow, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
            }
        }
        if (tabOverviewVisible || tabOverviewClosing) {
            if (tabOverviewWindow && IsWindowVisible(tabOverviewWindow)) {
                InvalidateRect(tabOverviewWindow, nullptr, FALSE);
                RedrawWindow(tabOverviewWindow, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
            }
        }

        RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE);

        // Bounded asynchronous retries allow the compositor to publish its new
        // backing surface without monopolising the UI thread for several seconds.
        if (windowSurfaceRecoveryTicks >= 18) {
            KillTimer(window, kWindowSurfaceRecoveryTimerId);
            windowSurfaceRecoveryActive = false;
            windowSurfaceRecoveryRefreshScale = false;
            windowSurfaceRecoveryRefreshView = true;
            windowSurfaceRecoveryRefreshLayout = true;
            windowSurfaceRecoveryPreserveOverlayStack = false;
        }
    }

    void updateActiveStartPageSidebarOffset()
    {
        auto* tab = active();
        if (!tab || tab->internalPage != L"start")
            return;

        const int offset = sidebarOpen ? 150 : 0;
        const std::string script = "(()=>{const p=document.querySelector('.page');if(p)p.style.transform='translate3d("
            + std::to_string(offset) + "px,0,0)';})();";
        auto javascript = adoptWK(WKStringCreateWithUTF8CString(script.c_str()));
        if (javascript) {
            if (auto page = WKViewGetPage(tab->view.get()))
                WKPageEvaluateJavaScriptInMainFrame(page, javascript.get(), nullptr, nullptr);
        }
    }

    void toggleSidebar()
    {
        if (!window)
            return;

        hoveredSidebarRow = -1;
        hoveredSidebarSettings = false;
        sidebarOverlayAnimationFrameReady = false;

        const float from = sidebarProgress();
        sidebarOpen = !sidebarOpen;
        updateActiveStartPageSidebarOffset();
        sidebarAnimationFrom = from;
        sidebarAnimationTarget = sidebarOpen ? 1.0f : 0.0f;
        sidebarAnimationStart = GetTickCount64();
        sidebarAnimating = std::abs(sidebarAnimationTarget - sidebarAnimationFrom) > 0.001f;
        if (sidebarAnimating)
            startChromeAnimationTimer();
        sidebarPaintRecoveryTicks = sidebarOpen ? 3 : 0;

        // Only the independent sidebar overlay changes geometry during this
        // transition. Do not call resize()/redrawAfterLayoutChange(): those paths
        // resize and synchronously repaint the WebKit GPU surface.
        positionOverlayWindows();
        updateSidebarZOrderTimer();
        const RECT buttonRect = sidebarButtonRect();
        InvalidateRect(window, &buttonRect, FALSE);
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
        if (nextHovered != hoveredToolbarButton) {
            const int previousHovered = hoveredToolbarButton;
            hoveredToolbarButton = nextHovered;
            for (int i = 0; i < 4; ++i)
                toolbarHoverOpacity[i] = i == nextHovered ? 1.0f : 0.0f;

            if (previousHovered >= 0) {
                const RECT previousRect = rightToolbarButtonRect(previousHovered);
                if (previousRect.right > previousRect.left && previousRect.bottom > previousRect.top)
                    InvalidateRect(window, &previousRect, FALSE);
            }
            if (nextHovered >= 0) {
                const RECT nextRect = rightToolbarButtonRect(nextHovered);
                if (nextRect.right > nextRect.left && nextRect.bottom > nextRect.top)
                    InvalidateRect(window, &nextRect, FALSE);
            }
        }

        int nextLeftControl = -1;
        for (int i : { 0, 2 }) {
            const RECT control = leftToolbarControlRect(i);
            if (control.right > control.left && control.bottom > control.top && PtInRect(&control, point)) {
                nextLeftControl = i;
                break;
            }
        }
        if (nextLeftControl != hoveredLeftControl) {
            const int previous = hoveredLeftControl;
            hoveredLeftControl = nextLeftControl;
            if (previous >= 0) {
                const RECT rect = leftToolbarControlRect(previous);
                if (rect.right > rect.left && rect.bottom > rect.top)
                    InvalidateRect(window, &rect, FALSE);
            }
            if (nextLeftControl >= 0) {
                const RECT rect = leftToolbarControlRect(nextLeftControl);
                if (rect.right > rect.left && rect.bottom > rect.top)
                    InvalidateRect(window, &rect, FALSE);
            }
        }

        int nextNavigationButton = -1;
        const RECT navigationRect = navigationPillRect();
        if (point.y >= toolbarTop && point.y < toolbarBottom && PtInRect(&navigationRect, point))
            nextNavigationButton = point.x < navigationRect.left + (navigationRect.right - navigationRect.left) / 2 ? 0 : 1;
        if (nextNavigationButton != hoveredNavigationButton) {
            hoveredNavigationButton = nextNavigationButton;
            InvalidateRect(window, &navigationRect, FALSE);
        }

        int nextSidebarRow = -1;
        if (sidebarOpen) {
            const int sidebarWidth = scaleForDpi(window, 300);
            const int rowTop = contentTop() + scaleForDpi(window, 88);
            const int rowHeight = scaleForDpi(window, 46);
            const RECT settings = sidebarSettingsRect();
            const int listBottom = settings.top - scaleForDpi(window, 8);
            if (point.x >= 0 && point.x < sidebarWidth && point.y >= rowTop && point.y < listBottom) {
                const int row = (point.y - rowTop) / std::max(1, rowHeight);
                const size_t count = sidebarSection == 0 ? bookmarks.size()
                    : sidebarSection == 1 ? readingList.size() : visits.size();
                const size_t visible = std::min<size_t>(10,
                    listBottom > rowTop ? static_cast<size_t>((listBottom - rowTop) / rowHeight) : 0);
                if (row >= 0 && static_cast<size_t>(row) < count && static_cast<size_t>(row) < visible)
                    nextSidebarRow = row;
            }
        }
        if (sidebarAnimating)
            nextSidebarRow = -1;
        if (nextSidebarRow != hoveredSidebarRow) {
            const int previous = hoveredSidebarRow;
            hoveredSidebarRow = nextSidebarRow;
            if (previous >= 0) {
                const RECT rect = sidebarRowRect(previous);
                invalidateSidebarOverlayRect(rect);
            }
            if (nextSidebarRow >= 0) {
                const RECT rect = sidebarRowRect(nextSidebarRow);
                invalidateSidebarOverlayRect(rect);
            }
        }

        const RECT sidebarSettings = sidebarSettingsRect();
        const bool nextSidebarSettings = sidebarOpen && !sidebarAnimating && PtInRect(&sidebarSettings, point);
        if (nextSidebarSettings != hoveredSidebarSettings) {
            hoveredSidebarSettings = nextSidebarSettings;
            invalidateSidebarOverlayRect(sidebarSettings);
        }

        int nextTrafficLight = -1;
        for (int i = 0; i < 3; ++i) {
            RECT painted = trafficLightHitRect(i);
            const int hoverPadding = scaleForDpi(window, 6);
            InflateRect(&painted, -hoverPadding, -hoverPadding);
            if (PtInRect(&painted, point)) {
                nextTrafficLight = i;
                break;
            }
        }
        if (nextTrafficLight != hoveredTrafficLight) {
            const int previous = hoveredTrafficLight;
            hoveredTrafficLight = nextTrafficLight;
            if (previous >= 0) {
                const RECT rect = trafficLightHitRect(previous);
                InvalidateRect(window, &rect, FALSE);
            }
            if (nextTrafficLight >= 0) {
                const RECT rect = trafficLightHitRect(nextTrafficLight);
                InvalidateRect(window, &rect, FALSE);
            }
        }

        int nextOverviewTab = -1;
        bool nextOverviewNewTab = false;
        if (tabOverviewVisible) {
            const RECT overviewArea = tabOverviewAreaRect();
            if (PtInRect(&overviewArea, point)) {
                const RECT newTabRect = tabOverviewNewTabRect();
                if (PtInRect(&newTabRect, point))
                    nextOverviewNewTab = true;
                else {
                    for (size_t i = 0; i < tabs.size(); ++i) {
                        const RECT cardRect = tabOverviewCardRect(i);
                        if (PtInRect(&cardRect, point)) {
                            nextOverviewTab = static_cast<int>(i);
                            break;
                        }
                    }
                }
            }
        }
        if (nextOverviewTab != hoveredOverviewTab || nextOverviewNewTab != hoveredOverviewNewTab) {
            hoveredOverviewTab = nextOverviewTab;
            hoveredOverviewNewTab = nextOverviewNewTab;
            if (tabOverviewWindow && IsWindowVisible(tabOverviewWindow))
                InvalidateRect(tabOverviewWindow, nullptr, FALSE);
        }
    }

    float tabSwitchEase() const
    {
        if (!tabSwitchAnimating)
            return 1.0f;
        const ULONGLONG elapsed = GetTickCount64() - tabSwitchAnimationStart;
        const float t = std::clamp(static_cast<float>(elapsed) / 180.0f, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float tabOverviewEaseAt(ULONGLONG start) const
    {
        const ULONGLONG duration = std::max<ULONGLONG>(1, tabOverviewTransitionDuration);
        const ULONGLONG elapsed = GetTickCount64() - start;
        const float t = std::clamp(static_cast<float>(elapsed) / static_cast<float>(duration),
            0.0f, 1.0f);
        // Quintic smoothstep gives both zero velocity and zero acceleration at the ends.
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float tabOverviewEase() const
    {
        return tabOverviewEaseAt(tabOverviewAnimationStart);
    }

    float tabOverviewClosingEase() const
    {
        return tabOverviewEaseAt(tabOverviewClosingStart);
    }

    float tabOverviewZoomProgress() const
    {
        if (!tabOverviewAnimating && !tabOverviewClosing)
            return tabOverviewZoomTo;
        const float eased = tabOverviewClosing
            ? tabOverviewClosingEase() : tabOverviewEase();
        return std::clamp(tabOverviewZoomFrom
            + (tabOverviewZoomTo - tabOverviewZoomFrom) * eased, 0.0f, 1.0f);
    }

    ULONGLONG tabOverviewDurationForDistance(float from, float to) const
    {
        // Keep reversals animated, but scale duration to the remaining distance.
        return std::max<ULONGLONG>(120,
            static_cast<ULONGLONG>(std::lround(420.0f * std::abs(to - from))));
    }

    int tabOverviewMinimumFrames() const
    {
        const ULONGLONG duration = std::max<ULONGLONG>(1, tabOverviewTransitionDuration);
        return std::max(4, static_cast<int>((duration * 60ULL * 72ULL) / 100000ULL));
    }

    void animateChrome()
    {
        if (!window)
            return;

        bool continueAnimation = false;
        for (int i = 0; i < 4; ++i) {
            const RECT button = rightToolbarButtonRect(i);
            const float target = hoveredToolbarButton == i && button.right > button.left ? 1.0f : 0.0f;
            const float before = toolbarHoverOpacity[i];
            const float delta = target - before;
            if (delta > -0.015f && delta < 0.015f)
                toolbarHoverOpacity[i] = target;
            else {
                toolbarHoverOpacity[i] += delta * 0.28f;
                continueAnimation = true;
            }
            if (std::abs(toolbarHoverOpacity[i] - before) > 0.001f
                && button.right > button.left && button.bottom > button.top)
                InvalidateRect(window, &button, FALSE);
        }

        if (tabSwitchAnimating) {
            const bool transitionStillActive = GetTickCount64() - tabSwitchAnimationStart < 180;
            const RECT activeRect = tabBarItemRect(activeTab);
            if (activeRect.right > activeRect.left)
                InvalidateRect(window, &activeRect, FALSE);
            if (previousActiveTab < tabs.size() && previousActiveTab != activeTab) {
                const RECT previousRect = tabBarItemRect(previousActiveTab);
                if (previousRect.right > previousRect.left)
                    InvalidateRect(window, &previousRect, FALSE);
            }
            if (transitionStillActive)
                continueAnimation = true;
            else
                tabSwitchAnimating = false;
        }

        if (tabOverviewAnimating) {
            const ULONGLONG elapsed = GetTickCount64() - tabOverviewAnimationStart;
            const bool transitionStillActive = elapsed < tabOverviewTransitionDuration
                || tabOverviewOpeningFramesRendered < tabOverviewMinimumFrames();
            if (tabOverviewWindow && IsWindowVisible(tabOverviewWindow))
                InvalidateRect(tabOverviewWindow, nullptr, FALSE);
            if (transitionStillActive) {
                continueAnimation = true;
            } else {
                tabOverviewAnimating = false;
                if (tabOverviewWindow && IsWindowVisible(tabOverviewWindow)) {
                    const int left = sidebarOpen ? scaleForDpi(window, 300) : 0;
                    SetWindowPos(tabOverviewWindow, nullptr, left, contentTop(), 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                    InvalidateRect(tabOverviewWindow, nullptr, FALSE);
                }
            }
        }

        if (tabOverviewClosing) {
            const ULONGLONG now = GetTickCount64();
            const ULONGLONG elapsed = now - tabOverviewClosingStart;
            const bool transitionStillActive = elapsed < tabOverviewTransitionDuration
                || tabOverviewClosingFramesRendered < tabOverviewMinimumFrames();
            if (tabOverviewWindow && IsWindowVisible(tabOverviewWindow))
                InvalidateRect(tabOverviewWindow, nullptr, FALSE);
            if (transitionStillActive) {
                continueAnimation = true;
            } else if (!tabOverviewClosingSettling) {
                // Let WebKit repaint asynchronously while the overview snapshot
                // still covers it. RDW_UPDATENOW can block this UI thread waiting on
                // an accelerated surface, freezing both the zoom and the browser.
                SetFocus(window);
                if (auto* selected = active()) {
                    if (HWND viewWindow = WKViewGetWindow(selected->view.get()))
                        InvalidateRect(viewWindow, nullptr, FALSE);
                }
                tabOverviewClosingSettling = true;
                tabOverviewClosingSettleStart = now;
                tabOverviewClosingSettleFramesRendered = 0;
                if (tabOverviewWindow && IsWindowVisible(tabOverviewWindow))
                    InvalidateRect(tabOverviewWindow, nullptr, FALSE);
                continueAnimation = true;
            } else {
                // A short asynchronous settle is enough to let queued WebKit paints
                // start without adding a visible dead pause after the zoom animation.
                const bool settleStillActive = now - tabOverviewClosingSettleStart < 72
                    || tabOverviewClosingSettleFramesRendered < 3;
                if (settleStillActive) {
                    continueAnimation = true;
                } else {
                    tabOverviewClosing = false;
                    tabOverviewClosingSettling = false;
                    tabOverviewClosingTab = -1;
                    tabOverviewZoomFrom = 1.0f;
                    tabOverviewZoomTo = 1.0f;
                    tabOverviewAnimationFrameReady = false;
                    if (tabOverviewWindow && IsWindow(tabOverviewWindow))
                        ShowWindow(tabOverviewWindow, SW_HIDE);
                    positionOverlayWindows(true);
                    // Return keyboard focus only after the transition overlay is gone.
                    // No synchronous redraw is requested here.
                    if (auto* selected = active()) {
                        if (HWND viewWindow = WKViewGetWindow(selected->view.get()))
                            SetFocus(viewWindow);
                    }
                }
            }
        }

        if (sidebarAnimating) {
            const bool transitionStillActive = GetTickCount64() - sidebarAnimationStart < 240;
            // Animate only the independent sidebar overlay. The WebKit HWND is
            // deliberately untouched both during the motion and at completion.
            positionOverlayWindows();
            if (transitionStillActive) {
                continueAnimation = true;
            } else {
                sidebarAnimating = false;
                positionOverlayWindows();
                const RECT buttonRect = sidebarButtonRect();
                InvalidateRect(window, &buttonRect, FALSE);
                // Opening only covers the existing page; forcing a WebKit repaint here
                // can blank its accelerated surface underneath the drawer. Refresh only
                // after closing, when page pixels are exposed again.
                if (!sidebarOpen)
                    refreshActivePageAfterSidebarTransition();
            }
        }

        // Do not invalidate the whole chrome band here: it includes Smart Search,
        // whose surface was visibly flashing during unrelated hover transitions.
        if (!continueAnimation) {
            KillTimer(window, kChromeAnimationTimerId);
            chromeAnimationTimerActive = false;
        }
    }

    RECT tabOverviewAreaRect() const
    {
        RECT client { };
        GetClientRect(window, &client);
        // During the sidebar slide, keep the overview's backing canvas at the same
        // origin and let the sidebar overlay cover it. Apply the inset after settling.
        const int left = sidebarAnimating ? 0
            : (sidebarOpen ? scaleForDpi(window, 300) : 0);
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

    bool captureTabThumbnail(TabState& tab, bool allowPreviouslyLoadedPartial = false)
    {
        // Capture only while this tab owns the visible WebKit surface.
        if (!tab.browser || tab.browser->active() != &tab || IsIconic(tab.browser->window)
            || tab.browser->sidebarOpen || tab.browser->sidebarAnimating
            || tab.browser->tabOverviewVisible || tab.browser->tabOverviewAnimating
            || tab.browser->tabOverviewClosing || tab.browser->findBarVisible
            || (tab.browser->downloadsPopoverWindow && IsWindowVisible(tab.browser->downloadsPopoverWindow))
            || (tab.browser->sharePopoverWindow && IsWindowVisible(tab.browser->sharePopoverWindow)))
            return false;

        auto page = WKViewGetPage(tab.view.get());
        if (!page)
            return false;
        const double loadProgress = WKPageGetEstimatedProgress(page);
        // For a tab-switch capture during a reload, a previously-completed render
        // of the same URL is safe to reuse. Never label old-page pixels with a URL
        // that has not completed its first render.
        if (loadProgress < 1.0
            && (!allowPreviouslyLoadedPartial
                || tab.lastCompletedThumbnailURL != tab.activeUrl))
            return false;
        HWND viewWindow = WKViewGetWindow(tab.view.get());
        if (!viewWindow || !IsWindowVisible(viewWindow))
            return false;
        RECT screenRect { };
        if (!GetWindowRect(viewWindow, &screenRect))
            return false;

        const int sourceWidth = static_cast<int>(screenRect.right - screenRect.left);
        const int sourceHeight = static_cast<int>(screenRect.bottom - screenRect.top);
        if (sourceWidth < 80 || sourceHeight < 60)
            return false;

        // Capture the already-presented pixels. Synthetic PrintWindow paints can
        // return stale or blank content for accelerated WebKit child windows.
        HDC sourceDC = GetDC(nullptr);
        if (!sourceDC)
            return false;
        HDC captureDC = CreateCompatibleDC(sourceDC);
        HBITMAP captureBitmap = CreateCompatibleBitmap(sourceDC, sourceWidth, sourceHeight);
        if (!captureDC || !captureBitmap) {
            if (captureBitmap) DeleteObject(captureBitmap);
            if (captureDC) DeleteDC(captureDC);
            ReleaseDC(nullptr, sourceDC);
            return false;
        }

        HGDIOBJ oldCapture = SelectObject(captureDC, captureBitmap);
        RECT captureRect { 0, 0, sourceWidth, sourceHeight };
        HBRUSH initialCaptureBrush = CreateSolidBrush(RGB(8, 12, 18));
        FillRect(captureDC, &captureRect, initialCaptureBrush);
        DeleteObject(initialCaptureBrush);

        const BOOL copied = BitBlt(captureDC, 0, 0, sourceWidth, sourceHeight,
            sourceDC, screenRect.left, screenRect.top, SRCCOPY | CAPTUREBLT);
        bool looksBlank = false;
        if (copied) {
            int validSamples = 0;
            int nearBlackSamples = 0;
            int nearWhiteSamples = 0;
            int minLuminance = 255;
            int maxLuminance = 0;
            for (int sy = 1; sy <= 7; ++sy) {
                const int y = (sourceHeight * sy) / 8;
                for (int sx = 1; sx <= 9; ++sx) {
                    const int x = (sourceWidth * sx) / 10;
                    const COLORREF pixel = GetPixel(captureDC, x, y);
                    if (pixel == CLR_INVALID)
                        continue;
                    const int luminance = (GetRValue(pixel) * 30 + GetGValue(pixel) * 59
                        + GetBValue(pixel) * 11) / 100;
                    ++validSamples;
                    minLuminance = std::min(minLuminance, luminance);
                    maxLuminance = std::max(maxLuminance, luminance);
                    if (GetRValue(pixel) < 10 && GetGValue(pixel) < 10 && GetBValue(pixel) < 12)
                        ++nearBlackSamples;
                    if (GetRValue(pixel) > 245 && GetGValue(pixel) > 245 && GetBValue(pixel) > 245)
                        ++nearWhiteSamples;
                }
            }
            looksBlank = validSamples >= 20
                && maxLuminance - minLuminance <= 9
                && (nearBlackSamples * 100 >= validSamples * 94
                    || nearWhiteSamples * 100 >= validSamples * 94);
        }

        bool saved = false;
        if (copied && !looksBlank) {
            const double factor = std::min(1.0,
                std::min(1920.0 / sourceWidth, 1080.0 / sourceHeight));
            const int targetWidth = std::max(1, static_cast<int>(sourceWidth * factor));
            const int targetHeight = std::max(1, static_cast<int>(sourceHeight * factor));
            HDC previewDC = CreateCompatibleDC(sourceDC);
            HBITMAP preview = CreateCompatibleBitmap(sourceDC, targetWidth, targetHeight);
            if (previewDC && preview) {
                HGDIOBJ oldPreview = SelectObject(previewDC, preview);
                SetStretchBltMode(previewDC, HALFTONE);
                SetBrushOrgEx(previewDC, 0, 0, nullptr);
                const BOOL scaled = StretchBlt(previewDC, 0, 0, targetWidth, targetHeight,
                    captureDC, 0, 0, sourceWidth, sourceHeight, SRCCOPY);
                SelectObject(previewDC, oldPreview);
                if (scaled) {
                    if (tab.previewBitmap)
                        DeleteObject(tab.previewBitmap);
                    tab.previewBitmap = preview;
                    tab.previewWidth = targetWidth;
                    tab.previewHeight = targetHeight;
                    tab.previewURL = tab.activeUrl;
                    tab.thumbnailCaptureNeeded = false;
                    saved = true;
                    if (tab.browser->tabOverviewVisible && tab.browser->tabOverviewWindow) {
                        tab.browser->tabOverviewAnimationFrameReady = false;
                        InvalidateRect(tab.browser->tabOverviewWindow, nullptr, FALSE);
                    }
                } else {
                    DeleteObject(preview);
                }
            } else if (preview) {
                DeleteObject(preview);
            }
            if (previewDC)
                DeleteDC(previewDC);
        }

        SelectObject(captureDC, oldCapture);
        DeleteObject(captureBitmap);
        DeleteDC(captureDC);
        ReleaseDC(nullptr, sourceDC);
        return saved;
    }

    void scheduleTabThumbnailCapture(TabState* tab, UINT delayMilliseconds = 420)
    {
        if (!window || !tab)
            return;
        pendingThumbnailCaptureTab = tab;
        tab->thumbnailCaptureNeeded = true;
        pendingThumbnailCaptureAttempts = 0;
        pendingThumbnailCaptureURL = tab->activeUrl;
        SetTimer(window, kDeferredThumbnailCaptureTimerId,
            std::max<UINT>(1, delayMilliseconds), nullptr);
    }

    void clearPendingThumbnailCapture()
    {
        pendingThumbnailCaptureTab = nullptr;
        pendingThumbnailCaptureAttempts = 0;
        pendingThumbnailCaptureURL.clear();
        if (window)
            KillTimer(window, kDeferredThumbnailCaptureTimerId);
    }

    void servicePendingTabThumbnailCapture()
    {
        TabState* pending = pendingThumbnailCaptureTab;
        if (!window || !pending)
            return;
        if (active() != pending) {
            clearPendingThumbnailCapture();
            return;
        }

        if (pendingThumbnailCaptureURL != pending->activeUrl) {
            pendingThumbnailCaptureURL = pending->activeUrl;
            pendingThumbnailCaptureAttempts = 0;
        }

        const auto retryCapture = [this]() {
            ++pendingThumbnailCaptureAttempts;
            if (pendingThumbnailCaptureAttempts >= 12) {
                clearPendingThumbnailCapture();
                return;
            }
            // Load completion can precede the final GPU presentation. Retry rather
            // than leaving a failed capture as a permanent skeleton preview.
            SetTimer(window, kDeferredThumbnailCaptureTimerId,
                pendingThumbnailCaptureAttempts < 3 ? 100 : 180, nullptr);
        };

        auto page = WKViewGetPage(pending->view.get());
        if (!page || WKPageGetEstimatedProgress(page) < 1.0) {
            retryCapture();
            return;
        }

        const std::wstring completedURL = createString(adoptWK(WKPageCopyActiveURL(page)).get());
        if (!completedURL.empty())
            pending->lastCompletedThumbnailURL = completedURL;

        const bool overlayBusy = IsIconic(window) || sidebarOpen || sidebarAnimating
            || tabOverviewVisible || tabOverviewAnimating || tabOverviewClosing
            || findBarVisible
            || (downloadsPopoverWindow && IsWindowVisible(downloadsPopoverWindow))
            || (sharePopoverWindow && IsWindowVisible(sharePopoverWindow));
        if (overlayBusy) {
            SetTimer(window, kDeferredThumbnailCaptureTimerId, 120, nullptr);
            return;
        }

        if (captureTabThumbnail(*pending)) {
            clearPendingThumbnailCapture();
            return;
        }
        retryCapture();
    }

    void maintainSidebarZOrder()
    {
        if (!window || (!sidebarOpen && !sidebarAnimating)) {
            if (window)
                KillTimer(window, kSidebarZOrderTimerId);
            sidebarZOrderTimerActive = false;
            sidebarPaintRecoveryTicks = 0;
            return;
        }
        if (IsIconic(window))
            return;

        if (!sidebarOverlayWindow || !IsWindow(sidebarOverlayWindow)) {
            positionOverlayWindows(true);
            return;
        }

        HWND activeView = nullptr;
        if (auto* selected = active())
            activeView = WKViewGetWindow(selected->view.get());
        if (!activeView || !IsWindow(activeView))
            return;

        // Child HWNDs are enumerated from the top of the Z order down.
        // The drawer must sit above both WebKit and the overview canvas; checking only
        // WebKit allowed the overview HWND to cover the sidebar during its slide.
        const bool overviewVisible = tabOverviewWindow && IsWindowVisible(tabOverviewWindow);
        int sidebarOrder = -1;
        int viewOrder = -1;
        int overviewOrder = -1;
        int order = 0;
        for (HWND child = GetWindow(window, GW_CHILD); child;
             child = GetWindow(child, GW_HWNDNEXT), ++order) {
            if (child == sidebarOverlayWindow)
                sidebarOrder = order;
            if (child == activeView)
                viewOrder = order;
            if (overviewVisible && child == tabOverviewWindow)
                overviewOrder = order;
            if (sidebarOrder >= 0 && viewOrder >= 0
                && (!overviewVisible || overviewOrder >= 0))
                break;
        }

        const bool behindActiveView = viewOrder >= 0 && sidebarOrder > viewOrder;
        const bool behindOverview = overviewVisible && overviewOrder >= 0
            && sidebarOrder > overviewOrder;
        if (sidebarOrder < 0 || behindActiveView || behindOverview) {
            // Restore the drawer above WebKit/overview. Since NOREDRAW is used to
            // avoid a synchronous erase flash, explicitly queue a paint afterward.
            SetWindowPos(sidebarOverlayWindow, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                    | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_SHOWWINDOW);
            InvalidateRect(sidebarOverlayWindow, nullptr, FALSE);
        }

        // Retry only a few asynchronous paints when the drawer is newly opened.
        // Never force an immediate paint here: that can stall animation or flash
        // the WebKit surface underneath.
        if (sidebarPaintRecoveryTicks > 0 && IsWindowVisible(sidebarOverlayWindow)) {
            RedrawWindow(sidebarOverlayWindow, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
            --sidebarPaintRecoveryTicks;
        }
    }

    void updateSidebarZOrderTimer()
    {
        if (!window)
            return;
        if (sidebarOpen || sidebarAnimating) {
            if (!sidebarZOrderTimerActive)
                sidebarZOrderTimerActive = SetTimer(window, kSidebarZOrderTimerId, 72, nullptr) != 0;
            maintainSidebarZOrder();
        } else {
            KillTimer(window, kSidebarZOrderTimerId);
            sidebarZOrderTimerActive = false;
        }
    }

    void drawStartPageThumbnail(HDC dc, const RECT& preview) const
    {
        const int width = static_cast<int>(preview.right - preview.left);
        const int height = static_cast<int>(preview.bottom - preview.top);
        if (width <= 0 || height <= 0)
            return;
        const int savedState = SaveDC(dc);
        IntersectClipRect(dc, preview.left, preview.top, preview.right, preview.bottom);

        HBRUSH background = CreateSolidBrush(RGB(11, 31, 52));
        FillRect(dc, &preview, background);
        DeleteObject(background);

        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        const RECT tealOuter { preview.left - width / 7, preview.top - height / 2,
            preview.left + width * 3 / 5, preview.top + height * 5 / 4 };
        HBRUSH tealBrush = CreateSolidBrush(RGB(17, 67, 76));
        HGDIOBJ oldBrush = SelectObject(dc, tealBrush);
        Ellipse(dc, tealOuter.left, tealOuter.top, tealOuter.right, tealOuter.bottom);
        SelectObject(dc, oldBrush);
        DeleteObject(tealBrush);

        const RECT tealInner { preview.left + width / 12, preview.top - height / 5,
            preview.left + width / 2, preview.top + height * 4 / 5 };
        HBRUSH tealInnerBrush = CreateSolidBrush(RGB(18, 89, 88));
        oldBrush = SelectObject(dc, tealInnerBrush);
        Ellipse(dc, tealInner.left, tealInner.top, tealInner.right, tealInner.bottom);
        SelectObject(dc, oldBrush);
        DeleteObject(tealInnerBrush);

        const RECT violetOuter { preview.left + width * 3 / 5, preview.top - height / 2,
            preview.right + width / 4, preview.top + height * 6 / 5 };
        HBRUSH violetBrush = CreateSolidBrush(RGB(42, 45, 91));
        oldBrush = SelectObject(dc, violetBrush);
        Ellipse(dc, violetOuter.left, violetOuter.top, violetOuter.right, violetOuter.bottom);
        SelectObject(dc, oldBrush);
        DeleteObject(violetBrush);
        SelectObject(dc, oldPen);

        RECT bottomShade { preview.left, preview.top + height * 3 / 4, preview.right, preview.bottom };
        HBRUSH bottomBrush = CreateSolidBrush(RGB(8, 24, 43));
        FillRect(dc, &bottomShade, bottomBrush);
        DeleteObject(bottomBrush);

        const int logoSize = std::max(scaleForDpi(window, 14), height / 8);
        RECT logoRect { preview.left + width / 2 - logoSize / 2,
            preview.top + height / 12, preview.left + width / 2 + logoSize / 2,
            preview.top + height / 12 + logoSize };
        drawCompass(dc, logoRect);

        HFONT titleFont = CreateFontW(-std::max(scaleForDpi(window, 11), height / 10),
            0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HGDIOBJ oldFont = SelectObject(dc, titleFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(246, 249, 255));
        RECT titleRect { preview.left + width / 4, logoRect.bottom + height / 80,
            preview.right - width / 4, logoRect.bottom + height / 6 };
        DrawTextW(dc, L"Aurora", -1, &titleRect, DT_CENTER | DT_TOP | DT_SINGLELINE);

        const int searchHeight = std::max(scaleForDpi(window, 10), height / 9);
        RECT searchRect { preview.left + width / 5, preview.top + height * 48 / 100,
            preview.right - width / 5, preview.top + height * 48 / 100 + searchHeight };
        drawRoundedControl(dc, searchRect, RGB(75, 103, 128), RGB(113, 143, 166),
            static_cast<float>(searchHeight) / 2.0f);

        const int tileSize = std::max(scaleForDpi(window, 8), height / 10);
        const int tileGap = std::max(scaleForDpi(window, 4), width / 70);
        constexpr int tileCount = 7;
        const int totalWidth = tileCount * tileSize + (tileCount - 1) * tileGap;
        const int startX = preview.left + (width - totalWidth) / 2;
        const int tileY = preview.top + height * 69 / 100;
        const COLORREF accents[tileCount] {
            RGB(40, 166, 169), RGB(231, 238, 249), RGB(32, 159, 185),
            RGB(223, 51, 67), RGB(63, 64, 70), RGB(227, 94, 43), RGB(234, 239, 246)
        };
        for (int i = 0; i < tileCount; ++i) {
            RECT tile { startX + i * (tileSize + tileGap), tileY,
                startX + i * (tileSize + tileGap) + tileSize, tileY + tileSize };
            drawRoundedControl(dc, tile, accents[i], accents[i],
                static_cast<float>(tileSize) * .22f);
        }
        if (titleFont) {
            SelectObject(dc, oldFont);
            DeleteObject(titleFont);
        }
        if (savedState)
            RestoreDC(dc, savedState);
    }

    void drawSettingsTabThumbnail(HDC dc, const RECT& preview, bool dark) const
    {
        const int width = static_cast<int>(preview.right - preview.left);
        const int height = static_cast<int>(preview.bottom - preview.top);
        if (width <= 0 || height <= 0)
            return;
        const int savedState = SaveDC(dc);
        IntersectClipRect(dc, preview.left, preview.top, preview.right, preview.bottom);

        const COLORREF base = dark ? RGB(37, 37, 40) : RGB(246, 247, 249);
        const COLORREF header = dark ? RGB(46, 46, 49) : RGB(252, 252, 253);
        const COLORREF surface = dark ? RGB(48, 48, 52) : RGB(255, 255, 255);
        const COLORREF line = dark ? RGB(69, 69, 73) : RGB(220, 223, 227);
        const COLORREF foreground = dark ? RGB(236, 236, 239) : RGB(42, 45, 49);
        const COLORREF muted = dark ? RGB(143, 143, 149) : RGB(131, 136, 143);

        HBRUSH background = CreateSolidBrush(base);
        FillRect(dc, &preview, background);
        DeleteObject(background);
        RECT headerRect { preview.left, preview.top, preview.right, preview.top + height / 5 };
        HBRUSH headerBrush = CreateSolidBrush(header);
        FillRect(dc, &headerRect, headerBrush);
        DeleteObject(headerBrush);

        HFONT titleFont = CreateFontW(-std::max(scaleForDpi(window, 10), height / 11),
            0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HGDIOBJ oldFont = SelectObject(dc, titleFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, foreground);
        RECT brandRect { preview.left + width / 24, preview.top + height / 30,
            preview.right - width / 24, preview.top + height / 7 };
        DrawTextW(dc, L"Aurora Settings", -1, &brandRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const int categoryY = preview.top + height / 5 + height / 14;
        const int categoryH = std::max(7, height / 12);
        const int categoryGap = std::max(3, width / 100);
        const int categoryW = (width - 2 * (width / 24) - 6 * categoryGap) / 7;
        for (int i = 0; i < 7; ++i) {
            RECT category { preview.left + width / 24 + i * (categoryW + categoryGap),
                categoryY, preview.left + width / 24 + i * (categoryW + categoryGap) + categoryW,
                categoryY + categoryH };
            const COLORREF fill = i == 0 ? (dark ? RGB(73, 73, 78) : RGB(224, 227, 232)) : header;
            drawRoundedControl(dc, category, fill, line, static_cast<float>(categoryH) / 3.0f);
        }

        const int bodyTop = categoryY + categoryH + height / 12;
        SetTextColor(dc, foreground);
        RECT sectionTitle { preview.left + width / 18, bodyTop, preview.right - width / 18,
            bodyTop + height / 9 };
        DrawTextW(dc, L"General", -1, &sectionTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const int fieldLeft = preview.left + width / 18;
        const int fieldRight = preview.right - width / 18;
        const int fieldHeight = std::max(8, height / 12);
        const int fieldGap = std::max(6, height / 18);
        for (int i = 0; i < 2; ++i) {
            const int y = bodyTop + height / 6 + i * (fieldHeight + fieldGap);
            RECT fieldLabel { fieldLeft, y, fieldLeft + width / 4, y + fieldHeight };
            RECT fieldValue { fieldLeft + width / 3, y - 2, fieldRight, y + fieldHeight + 2 };
            SetTextColor(dc, muted);
            DrawTextW(dc, i == 0 ? L"Homepage" : L"Downloads", -1, &fieldLabel,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            drawRoundedControl(dc, fieldValue, surface, line, static_cast<float>(fieldHeight) / 3.0f);
        }
        if (titleFont) {
            SelectObject(dc, oldFont);
            DeleteObject(titleFont);
        }
        if (savedState)
            RestoreDC(dc, savedState);
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

    void drawTabOverviewTransitionSnapshot(HDC dc, const TabState& tab, const RECT& destination) const
    {
        if (destination.right <= destination.left || destination.bottom <= destination.top)
            return;

        // Reuse the same captured pixels shown by the card, including internal pages,
        // so the moving snapshot and the settled preview do not visually swap midway.
        if (tab.previewBitmap && tab.previewWidth > 0 && tab.previewHeight > 0
            && tab.previewURL == tab.activeUrl) {
            HDC previewDC = CreateCompatibleDC(dc);
            if (previewDC) {
                HGDIOBJ oldPreview = SelectObject(previewDC, tab.previewBitmap);
                // Use the same smooth resampling as the settled card preview. Nearest-
                // neighbour sampling made text and fine edges shimmer during zoom.
                SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, 0, 0, nullptr);
                StretchBlt(dc, destination.left, destination.top,
                    destination.right - destination.left, destination.bottom - destination.top,
                    previewDC, 0, 0, tab.previewWidth, tab.previewHeight, SRCCOPY);
                SelectObject(previewDC, oldPreview);
                DeleteDC(previewDC);
                return;
            }
        }

        if (tab.internalPage == L"start") {
            drawStartPageThumbnail(dc, destination);
            return;
        }
        if (tab.internalPage == L"settings") {
            const bool tabDark = tab.hasInternalThemeSnapshot
                ? tab.internalDarkTheme : chromeDarkTheme();
            drawSettingsTabThumbnail(dc, destination, tabDark);
            return;
        }

        const bool placeholderDark = tab.hasInternalThemeSnapshot
            ? tab.internalDarkTheme : chromeDarkTheme();
        HBRUSH pageBrush = CreateSolidBrush(placeholderDark
            ? RGB(35, 39, 45) : RGB(250, 251, 253));
        FillRect(dc, &destination, pageBrush);
        DeleteObject(pageBrush);
        RECT fakeBar { destination.left, destination.top, destination.right,
            std::min(destination.bottom, destination.top + scaleForDpi(window, 20)) };
        HBRUSH barBrush = CreateSolidBrush(placeholderDark
            ? RGB(48, 52, 59) : RGB(235, 238, 243));
        FillRect(dc, &fakeBar, barBrush);
        DeleteObject(barBrush);
        if (destination.right - destination.left > scaleForDpi(window, 90)
            && destination.bottom - destination.top > scaleForDpi(window, 70)) {
            HBRUSH lineBrush = CreateSolidBrush(placeholderDark
                ? RGB(78, 83, 91) : RGB(222, 226, 232));
            const LONG left = destination.left + scaleForDpi(window, 16);
            const LONG lineWidth = std::max<LONG>(
                static_cast<LONG>(scaleForDpi(window, 22)),
                (destination.right - destination.left) * 2 / 3);
            for (LONG row = 0; row < 3; ++row) {
                const LONG y = fakeBar.bottom + scaleForDpi(window, 13 + row * 17);
                RECT line { left, y, std::min<LONG>(
                    destination.right - scaleForDpi(window, 14), left + lineWidth),
                    y + scaleForDpi(window, 4) };
                if (line.bottom < destination.bottom - scaleForDpi(window, 6))
                    FillRect(dc, &line, lineBrush);
            }
            DeleteObject(lineBrush);
        }
    }

    RECT tabOverviewZoomCardRect(size_t index) const
    {
        const RECT area = tabOverviewAreaRect();
        const RECT card = tabOverviewCardRect(index);
        const int inset = scaleForDpi(window, 8);
        const int metaHeight = scaleForDpi(window, 58);
        RECT bounds {
            card.left + inset - area.left,
            card.top + inset - area.top,
            card.right - inset - area.left,
            card.bottom - metaHeight - area.top
        };

        // Match the aspect-preserving image rectangle used by the settled card.
        // Otherwise the transition changes its scaling halfway through the handoff.
        if (index < tabs.size()) {
            const TabState& tab = *tabs[index];
            if (tab.previewBitmap && tab.previewWidth > 0 && tab.previewHeight > 0
                && tab.previewURL == tab.activeUrl) {
                const double boundsWidth = static_cast<double>(bounds.right - bounds.left);
                const double boundsHeight = static_cast<double>(bounds.bottom - bounds.top);
                const double fit = std::min(boundsWidth / tab.previewWidth,
                    boundsHeight / tab.previewHeight);
                const int fitWidth = std::max(1, static_cast<int>(std::lround(tab.previewWidth * fit)));
                const int fitHeight = std::max(1, static_cast<int>(std::lround(tab.previewHeight * fit)));
                const int fitLeft = bounds.left + (static_cast<int>(boundsWidth) - fitWidth) / 2;
                const int fitTop = bounds.top + (static_cast<int>(boundsHeight) - fitHeight) / 2;
                return { fitLeft, fitTop, fitLeft + fitWidth, fitTop + fitHeight };
            }
        }
        return bounds;
    }

    RECT tabOverviewZoomDestination(size_t index) const
    {
        const RECT area = tabOverviewAreaRect();
        const int width = std::max(1, static_cast<int>(area.right - area.left));
        const int height = std::max(1, static_cast<int>(area.bottom - area.top));
        const RECT card = tabOverviewZoomCardRect(index);
        const float zoom = tabOverviewZoomProgress();
        auto interpolate = [zoom](int from, int to) {
            return static_cast<int>(std::lround(static_cast<float>(from)
                + static_cast<float>(to - from) * zoom));
        };
        return {
            interpolate(card.left, 0),
            interpolate(card.top, 0),
            interpolate(card.right, width),
            interpolate(card.bottom, height)
        };
    }

    void drawTabOverviewClosingTransition(HDC dc)
    {
        if (!tabOverviewClosing || tabOverviewClosingTab < 0
            || static_cast<size_t>(tabOverviewClosingTab) >= tabs.size())
            return;
        const TabState& tab = *tabs[static_cast<size_t>(tabOverviewClosingTab)];
        drawTabOverviewTransitionSnapshot(dc, tab, tabOverviewZoomDestination(
            static_cast<size_t>(tabOverviewClosingTab)));
        if (tabOverviewClosingSettling)
            ++tabOverviewClosingSettleFramesRendered;
        else
            ++tabOverviewClosingFramesRendered;
    }

    void drawTabOverviewOpeningTransition(HDC dc)
    {
        if (!tabOverviewAnimating || tabs.empty() || activeTab >= tabs.size())
            return;
        const TabState& tab = *tabs[activeTab];
        drawTabOverviewTransitionSnapshot(dc, tab, tabOverviewZoomDestination(activeTab));
        ++tabOverviewOpeningFramesRendered;
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
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HFONT bodyFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HFONT urlFont = CreateFontW(-MulDiv(10, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, headingFont));
        SetBkMode(dc, TRANSPARENT);

        RECT titleRect { area.left + scaleForDpi(window, 24), area.top + scaleForDpi(window, 10),
            area.right - scaleForDpi(window, 160), area.top + scaleForDpi(window, 40) };
        SetTextColor(dc, dark ? RGB(240, 240, 242) : RGB(40, 43, 47));
        DrawTextW(dc, L"Tab Overview", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const RECT newTab = tabOverviewNewTabRect();
        HBRUSH newTabBrush = CreateSolidBrush(dark
            ? (hoveredOverviewNewTab ? RGB(72, 72, 76) : RGB(58, 58, 61))
            : (hoveredOverviewNewTab ? RGB(245, 247, 250) : RGB(255, 255, 255)));
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

            const bool cardHovered = hoveredOverviewTab == static_cast<int>(i);
            RECT shadowRect = card;
            OffsetRect(&shadowRect, 0, scaleForDpi(window, 3));
            const COLORREF shadowColor = dark ? RGB(23, 23, 25) : RGB(222, 225, 230);
            drawRoundedControl(dc, shadowRect, shadowColor, shadowColor,
                static_cast<float>(scaleForDpi(window, 10)));
            HBRUSH cardBrush = CreateSolidBrush(dark
                ? (i == activeTab ? RGB(53, 53, 56) : cardHovered ? RGB(49, 49, 53) : RGB(43, 43, 46))
                : (i == activeTab ? RGB(255, 255, 255) : cardHovered ? RGB(255, 255, 255) : RGB(253, 253, 254)));
            HPEN cardPen = CreatePen(PS_SOLID, 1, dark
                ? (i == activeTab ? RGB(102, 126, 164) : cardHovered ? RGB(85, 85, 92) : RGB(69, 69, 73))
                : (i == activeTab ? RGB(133, 166, 211) : cardHovered ? RGB(194, 204, 216) : RGB(220, 222, 226)));
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

            if (tabs[i]->previewBitmap && tabs[i]->previewWidth > 0
                && tabs[i]->previewHeight > 0
                && tabs[i]->previewURL == tabs[i]->activeUrl) {
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
            } else if (tabs[i]->internalPage == L"start") {
                // The Start Page has a deterministic native preview. Prefer it over
                // any stale capture from the accelerated WebKit surface, which can
                // be black after another tab has been active.
                drawStartPageThumbnail(dc, preview);
            } else if (tabs[i]->internalPage == L"settings") {
                const bool tabDark = tabs[i]->hasInternalThemeSnapshot
                    ? tabs[i]->internalDarkTheme : dark;
                drawSettingsTabThumbnail(dc, preview, tabDark);
            } else {
                const bool placeholderDark = tabs[i]->hasInternalThemeSnapshot
                    ? tabs[i]->internalDarkTheme : dark;
                HBRUSH pageBrush = CreateSolidBrush(placeholderDark
                    ? RGB(35, 39, 45) : RGB(250, 251, 253));
                FillRect(dc, &preview, pageBrush);
                DeleteObject(pageBrush);

                RECT fakeBar { preview.left, preview.top, preview.right,
                    preview.top + scaleForDpi(window, 20) };
                HBRUSH barBrush = CreateSolidBrush(placeholderDark
                    ? RGB(48, 52, 59) : RGB(235, 238, 243));
                FillRect(dc, &fakeBar, barBrush);
                DeleteObject(barBrush);

                const int dot = scaleForDpi(window, 4);
                for (int d = 0; d < 3; ++d) {
                    HBRUSH dotBrush = CreateSolidBrush(d == 0 ? RGB(238, 92, 88)
                        : d == 1 ? RGB(235, 185, 75) : RGB(78, 190, 116));
                    RECT dotRect { preview.left + scaleForDpi(window, 8) + d * scaleForDpi(window, 8),
                        preview.top + scaleForDpi(window, 7),
                        preview.left + scaleForDpi(window, 8) + d * scaleForDpi(window, 8) + dot,
                        preview.top + scaleForDpi(window, 7) + dot };
                    FillRect(dc, &dotRect, dotBrush);
                    DeleteObject(dotBrush);
                }

                RECT addressPill { preview.left + scaleForDpi(window, 38),
                    preview.top + scaleForDpi(window, 4),
                    preview.right - scaleForDpi(window, 10),
                    preview.top + scaleForDpi(window, 16) };
                drawRoundedControl(dc, addressPill,
                    placeholderDark ? RGB(60, 64, 72) : RGB(255, 255, 255),
                    placeholderDark ? RGB(75, 80, 88) : RGB(215, 220, 228),
                    static_cast<float>(scaleForDpi(window, 5)));

                const int contentLeft = preview.left + scaleForDpi(window, 12);
                const int contentTop = preview.top + scaleForDpi(window, 30);
                const int contentWidth = static_cast<int>(preview.right - preview.left)
                    - scaleForDpi(window, 24);
                if (tabs[i]->favicon) {
                    const int iconSize = scaleForDpi(window, 17);
                    DrawIconEx(dc, contentLeft, contentTop, tabs[i]->favicon,
                        iconSize, iconSize, 0, nullptr, DI_NORMAL);
                }

                HFONT placeholderFont = CreateFontW(-std::max(scaleForDpi(window, 10), 9),
                    0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
                HGDIOBJ oldPlaceholderFont = SelectObject(dc, placeholderFont);
                SetTextColor(dc, placeholderDark ? RGB(229, 232, 238) : RGB(40, 45, 53));
                RECT pageTitle { contentLeft + scaleForDpi(window, 23), contentTop,
                    preview.right - scaleForDpi(window, 10),
                    contentTop + scaleForDpi(window, 20) };
                const std::wstring title = tabs[i]->title.empty() ? L"Loading page" : tabs[i]->title;
                DrawTextW(dc, title.c_str(), -1, &pageTitle,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (placeholderFont) {
                    SelectObject(dc, oldPlaceholderFont);
                    DeleteObject(placeholderFont);
                }

                HBRUSH skeletonBrush = CreateSolidBrush(placeholderDark
                    ? RGB(70, 76, 85) : RGB(225, 230, 237));
                RECT hero { contentLeft, contentTop + scaleForDpi(window, 26),
                    contentLeft + contentWidth * 46 / 100,
                    contentTop + scaleForDpi(window, 59) };
                FillRect(dc, &hero, skeletonBrush);
                RECT line1 { contentLeft + contentWidth * 51 / 100,
                    contentTop + scaleForDpi(window, 29), preview.right - scaleForDpi(window, 12),
                    contentTop + scaleForDpi(window, 35) };
                RECT line2 { line1.left, contentTop + scaleForDpi(window, 41),
                    line1.right - scaleForDpi(window, 18), contentTop + scaleForDpi(window, 47) };
                RECT line3 { contentLeft, contentTop + scaleForDpi(window, 69),
                    preview.right - scaleForDpi(window, 36), contentTop + scaleForDpi(window, 75) };
                RECT line4 { contentLeft, contentTop + scaleForDpi(window, 83),
                    preview.right - scaleForDpi(window, 58), contentTop + scaleForDpi(window, 89) };
                FillRect(dc, &line1, skeletonBrush);
                FillRect(dc, &line2, skeletonBrush);
                FillRect(dc, &line3, skeletonBrush);
                FillRect(dc, &line4, skeletonBrush);
                DeleteObject(skeletonBrush);
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
        if (showFullWebsiteAddress) {
            std::wstring full = url;
            if (scheme != std::wstring::npos) {
                const size_t authorityStart = scheme + 3;
                const size_t authorityEnd = full.find_first_of(L"/?#", authorityStart);
                const size_t credentials = full.find(L'@', authorityStart);
                if (credentials != std::wstring::npos
                    && (authorityEnd == std::wstring::npos || credentials < authorityEnd))
                    full.erase(authorityStart, credentials - authorityStart + 1);
            }
            return full.rfind(L"file:", 0) == 0 ? L"Local File" : full;
        }
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
        if (!addressEditing || !addressBar || privateBrowsing || !searchSuggestionsEnabled) {
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
            const size_t begin = scheme == std::wstring::npos ? 0 : scheme + 3;
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
        std::wstring matchQuery = loweredQuery;
        if (matchQuery.rfind(L"https://", 0) == 0)
            matchQuery.erase(0, 8);
        else if (matchQuery.rfind(L"http://", 0) == 0)
            matchQuery.erase(0, 7);
        if (matchQuery.rfind(L"www.", 0) == 0)
            matchQuery.erase(0, 4);
        if (matchQuery.empty())
            matchQuery = loweredQuery;

        auto trimWhitespace = [](std::wstring value) {
            while (!value.empty() && iswspace(value.front()))
                value.erase(value.begin());
            while (!value.empty() && iswspace(value.back()))
                value.pop_back();
            return value;
        };
        auto normalizeSuggestionEntry = [&](VisitEntry entry) {
            entry.title = trimWhitespace(std::move(entry.title));
            const std::wstring normalizedTitle = lowercase(entry.title);
            if (normalizedTitle.empty() || normalizedTitle == L"new tab"
                || normalizedTitle == L"about:blank") {
                const std::wstring host = hostFor(entry.url);
                entry.title = host.empty() ? entry.url : host;
            }
            return entry;
        };
        addressSuggestionEntries.clear();
        selectedAddressSuggestion = -1;

        if (loweredQuery.empty()) {
            // With an empty field show the most frequently visited distinct sites.
            const auto frequent = frequentVisits(visits.size());
            std::vector<std::wstring> seenHosts;
            for (const auto& entry : frequent) {
                const std::wstring host = lowercase(hostFor(entry.url));
                if (host.empty() || std::find(seenHosts.begin(), seenHosts.end(), host) != seenHosts.end())
                    continue;
                seenHosts.push_back(host);
                addressSuggestionEntries.push_back(normalizeSuggestionEntry(entry));
                if (addressSuggestionEntries.size() >= 6)
                    break;
            }
        } else {
            struct RankedVisit {
                int rank { 99 };
                VisitEntry entry;
            };
            std::vector<VisitEntry> suggestionPool = visits;
            auto appendSavedSuggestions = [&](const std::vector<SavedPage>& pages) {
                for (const auto& page : pages) {
                    if (page.url.empty())
                        continue;
                    auto existing = std::find_if(suggestionPool.begin(), suggestionPool.end(),
                        [&](const VisitEntry& entry) { return entry.url == page.url; });
                    if (existing == suggestionPool.end())
                        suggestionPool.push_back({ page.url, page.title, 0, page.added });
                    else if (existing->title.empty())
                        existing->title = page.title;
                }
            };
            appendSavedSuggestions(bookmarks);
            appendSavedSuggestions(readingList);

            std::vector<RankedVisit> candidates;
            candidates.reserve(suggestionPool.size());

            auto startsAtWordBoundary = [](const std::wstring& value, const std::wstring& needle) {
                if (needle.empty())
                    return false;
                for (size_t pos = 0; pos + needle.size() <= value.size(); ++pos) {
                    if (value.compare(pos, needle.size(), needle) != 0)
                        continue;
                    if (pos == 0)
                        return true;
                    const wchar_t before = value[pos - 1];
                    if (iswspace(before) || before == L'-' || before == L'_' || before == L'.'
                        || before == L'/' || before == L'\\' || before == L':' || before == L'?'
                        || before == L'&' || before == L'=' || before == L'(' || before == L'[')
                        return true;
                }
                return false;
            };

            // Allow a small typo in a typed query to find an existing history entry.
            // Compare only against word prefixes, so a fuzzy match cannot surface an
            // unrelated page merely because the query appears somewhere in a long title.
            auto editDistance = [](const std::wstring& left, const std::wstring& right) {
                if (std::abs(static_cast<int>(left.size()) - static_cast<int>(right.size())) > 2)
                    return 99;
                std::vector<int> previous(right.size() + 1);
                std::vector<int> current(right.size() + 1);
                for (size_t j = 0; j <= right.size(); ++j)
                    previous[j] = static_cast<int>(j);
                for (size_t i = 1; i <= left.size(); ++i) {
                    current[0] = static_cast<int>(i);
                    for (size_t j = 1; j <= right.size(); ++j) {
                        const int substitution = previous[j - 1]
                            + (left[i - 1] == right[j - 1] ? 0 : 1);
                        current[j] = std::min({
                            previous[j] + 1,
                            current[j - 1] + 1,
                            substitution
                        });
                    }
                    previous.swap(current);
                }
                return previous[right.size()];
            };
            auto fuzzyPrefixDistance = [&](const std::wstring& value, const std::wstring& needle) {
                if (needle.size() < 5 || value.empty())
                    return 99;
                int best = 99;
                for (size_t start = 0; start < value.size(); ++start) {
                    if (start > 0) {
                        const wchar_t before = value[start - 1];
                        if (!(iswspace(before) || before == L'-' || before == L'_' || before == L'.'
                            || before == L'/' || before == L'\\' || before == L':' || before == L'('
                            || before == L'['))
                            continue;
                    }
                    size_t end = start;
                    while (end < value.size() && !iswspace(value[end]) && value[end] != L'.'
                        && value[end] != L'/' && value[end] != L'\\' && value[end] != L':'
                        && value[end] != L'?' && value[end] != L'&' && value[end] != L'='
                        && end - start <= needle.size() + 1)
                        ++end;
                    const size_t wordLength = end - start;
                    const size_t minLength = needle.size() > 2 ? needle.size() - 2 : 1;
                    const size_t maxLength = std::min(needle.size() + 2, wordLength);
                    if (maxLength < minLength)
                        continue;
                    for (size_t length = minLength; length <= maxLength; ++length) {
                        best = std::min(best, editDistance(needle, value.substr(start, length)));
                        if (best == 0)
                            return best;
                    }
                }
                return best;
            };

            for (const auto& entry : suggestionPool) {
                const std::wstring url = lowercase(entry.url);
                const std::wstring title = lowercase(entry.title);
                const std::wstring host = lowercase(hostFor(entry.url));
                const size_t scheme = url.find(L"://");
                const size_t authorityStart = scheme == std::wstring::npos ? 0 : scheme + 3;
                const size_t pathStart = url.find_first_of(L"/?#", authorityStart);
                const std::wstring path = pathStart == std::wstring::npos
                    ? L"" : url.substr(pathStart + 1);

                int rank = 99;
                if (host.rfind(matchQuery, 0) == 0)
                    rank = 0;
                else if (title.rfind(matchQuery, 0) == 0)
                    rank = 1;
                else if (matchQuery.size() >= 2 && startsAtWordBoundary(title, matchQuery))
                    rank = 2;
                else if (matchQuery.size() >= 3 && host.find(matchQuery) != std::wstring::npos)
                    rank = 3;
                else if (matchQuery.size() >= 3 && startsAtWordBoundary(path, matchQuery))
                    rank = 4;
                else if (loweredQuery.find(L"://") != std::wstring::npos
                    && url.rfind(loweredQuery, 0) == 0)
                    rank = 0;
                else if (matchQuery.size() >= 5) {
                    const size_t hostLabelEnd = host.find(L'.');
                    const std::wstring hostLabel = host.substr(0,
                        hostLabelEnd == std::wstring::npos ? hostLabelEnd : hostLabelEnd);
                    const int hostDistance = fuzzyPrefixDistance(hostLabel, matchQuery);
                    const int titleDistance = fuzzyPrefixDistance(title, matchQuery);
                    const int distance = std::min(hostDistance, titleDistance);
                    const int allowedEdits = matchQuery.size() >= 8 ? 2 : 1;
                    if (distance <= allowedEdits)
                        rank = 5 + distance;
                }

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
                addressSuggestionEntries.push_back(normalizeSuggestionEntry(candidate.entry));
                if (addressSuggestionEntries.size() >= 6)
                    break;
            }
            if (addressSuggestionEntries.empty()) {
                VisitEntry searchEntry;
                searchEntry.url = searchURL(query);
                searchEntry.title = L"Search for \"" + query + L"\"";
                searchEntry.searchAction = true;
                addressSuggestionEntries.push_back(std::move(searchEntry));
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
            popupWidth, popupHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
        // Repaint the entire resized popup immediately. Otherwise a stale client
        // area can remain visible beneath the suggestion rows after the list shrinks.
        RedrawWindow(addressSuggestionWindow, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
        UpdateWindow(addressSuggestionWindow);
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

        if (visits.size() > 256)
            visits.resize(256);

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

        auto normalizedHost = [](const std::wstring& url) {
            const size_t scheme = url.find(L"://");
            const size_t begin = scheme == std::wstring::npos ? 0 : scheme + 3;
            const size_t end = url.find_first_of(L"/?#:", begin);
            std::wstring host = url.substr(begin, end == std::wstring::npos ? end : end - begin);
            if (host.rfind(L"www.", 0) == 0)
                host.erase(0, 4);
            std::transform(host.begin(), host.end(), host.begin(),
                [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
            return host;
        };

        std::vector<VisitEntry> distinctSites;
        std::vector<std::wstring> seenHosts;
        distinctSites.reserve(std::min(maximum, sorted.size()));
        seenHosts.reserve(std::min(maximum, sorted.size()));
        for (const auto& entry : sorted) {
            const std::wstring host = normalizedHost(entry.url);
            if (host.empty() || std::find(seenHosts.begin(), seenHosts.end(), host) != seenHosts.end())
                continue;
            seenHosts.push_back(host);
            distinctSites.push_back(entry);
            if (distinctSites.size() >= maximum)
                break;
        }
        return distinctSites;
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
            // Older saved entries can contain embedded NULs from previously
            // over-allocated UTF-8 conversions. Keep them from truncating pages.
            url.erase(std::remove(url.begin(), url.end(), L'\0'), url.end());
            title.erase(std::remove(title.begin(), title.end(), L'\0'), title.end());
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
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily()
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
        html += dark ? R"CSS(:root{color-scheme:dark}body{margin:0;background:#1c1c1e;color:#f5f5f7;font-family:-apple-system,BlinkMacSystemFont,"Inter","Segoe UI Variable Text","Segoe UI",Arial,sans-serif}.item{background:#2c2c2e;border:1px solid #48484a;padding:16px;border-radius:12px;margin:10px 0}.muted{color:#a1a1a6})CSS" : R"CSS(:root{color-scheme:light}body{margin:0;background:#f5f6f7;color:#202428;font-family:-apple-system,BlinkMacSystemFont,"Inter","Segoe UI Variable Text","Segoe UI",Arial,sans-serif}.item{background:#fff;border:1px solid #e1e4e6;padding:16px;border-radius:12px;margin:10px 0}.muted{color:#73777d})CSS";
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
            tab.settingsSection = std::clamp(requestedSection, 0, 12);
        tab.internalPage = L"settings";
        tab.hasInternalThemeSnapshot = true;
        tab.internalDarkTheme = darkTheme();
        const bool dark = darkTheme();
        const std::string theme = themeMode == ThemeMode::System ? "system" : (themeMode == ThemeMode::Light ? "light" : "dark");
        const std::string engine = toUTF8(searchEngine);
        const std::string home = toUTF8(htmlEscape(homePage));
        const std::string downloadsPath = toUTF8(htmlEscape(downloadLocation));
        const std::string startPageBackgroundLabel = startPageBackground.empty()
            ? "Built-in Aurora wallpaper" : toUTF8(htmlEscape(startPageBackground));
        const std::string systemSelected = theme == "system" ? " selected" : "";
        const std::string lightSelected = theme == "light" ? " selected" : "";
        const std::string darkSelected = theme == "dark" ? " selected" : "";
        const std::string googleSelected = engine == "Google" ? " selected" : "";
        const std::string bingSelected = engine == "Bing" ? " selected" : "";
        const std::string duckSelected = engine == "DuckDuckGo" ? " selected" : "";
        const std::string yahooSelected = engine == "Yahoo" ? " selected" : "";
        const std::string ecosiaSelected = engine == "Ecosia" ? " selected" : "";
        const std::string newTabStartSelected = newTabsOpenWithHomepage ? "" : " selected";
        const std::string newTabHomeSelected = newTabsOpenWithHomepage ? " selected" : "";
        const std::string newWindowStartSelected = newWindowsOpenWithHomepage ? "" : " selected";
        const std::string newWindowHomeSelected = newWindowsOpenWithHomepage ? " selected" : "";
        const std::string activeGeneral = tab.settingsSection == 0 ? " active" : "";
        const std::string activeTabs = tab.settingsSection == 1 ? " active" : "";
        const std::string activeAutoFill = tab.settingsSection == 2 ? " active" : "";
        const std::string activePasswords = tab.settingsSection == 3 ? " active" : "";
        const std::string activeSearch = tab.settingsSection == 4 ? " active" : "";
        const std::string activeSecurity = tab.settingsSection == 5 ? " active" : "";
        const std::string activePrivacy = tab.settingsSection == 6 ? " active" : "";
        const std::string activeWebsites = tab.settingsSection == 7 ? " active" : "";
        const std::string activeProfiles = tab.settingsSection == 8 ? " active" : "";
        const std::string activeExtensions = tab.settingsSection == 9 ? " active" : "";
        const std::string activeAdvanced = tab.settingsSection == 10 ? " active" : "";
        const std::string activeAppearance = tab.settingsSection == 11 ? " active" : "";
        const std::string activeAbout = tab.settingsSection == 12 ? " active" : "";
        auto checked = [](bool value) { return value ? " checked" : ""; };
        std::string extensionItems;
        if (extensionScripts.empty()) {
            extensionItems = "<div class=\"checkrow\"><span>No user scripts installed</span><span class=\"muted\">0 items</span></div>";
        } else {
            for (const auto& extension : extensionScripts) {
                extensionItems += "<div class=\"checkrow\"><span>" + toUTF8(htmlEscape(extension.filename))
                    + "</span><span class=\"muted\">Enabled</span></div>";
            }
        }

        std::string html = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings</title><style>
:root{color-scheme:)HTML" + std::string(dark ? "dark" : "light") + R"HTML(;--bg:)HTML"
            + (dark ? "#1c1c1e" : "#f5f5f7") + R"HTML(;--panel:)HTML" + (dark ? "#262628" : "#fff")
            + R"HTML(;--glass:)HTML" + (dark ? "rgba(42,42,45,.78)" : "rgba(255,255,255,.78)")
            + R"HTML(;--glass-bar:)HTML" + (dark ? "rgba(33,33,36,.86)" : "rgba(247,248,250,.82)")
            + R"HTML(;--bar:)HTML" + (dark ? "#222224" : "#f5f5f7") + R"HTML(;--text:)HTML"
            + (dark ? "#f5f5f7" : "#1d1d1f") + R"HTML(;--muted:)HTML" + (dark ? "#a1a1a6" : "#6e6e73")
            + R"HTML(;--line:)HTML" + (dark ? "#414144" : "#dedee2") + R"HTML(;--selected:)HTML"
            + (dark ? "#3b3b3f" : "#e6e6ea") + R"HTML(;--blue:)HTML" + (dark ? "#8ab4f8" : "#1769d2")
            + R"HTML(}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;font-family:"Segoe UI Variable","Segoe UI",system-ui,sans-serif;font-size:13px;line-height:1.45;-webkit-font-smoothing:antialiased;background:radial-gradient(ellipse at 12% 0%,rgba(30,160,190,.12),transparent 45%),radial-gradient(ellipse at 88% 8%,rgba(133,98,230,.12),transparent 42%),var(--bg);color:var(--text)}
form{margin:0}
button,input,select,a{font-family:"Segoe UI Variable","Segoe UI",system-ui,sans-serif}
.shell{width:min(1040px,calc(100% - 48px));margin:0 auto 32px;background:var(--glass);backdrop-filter:blur(24px) saturate(1.3);-webkit-backdrop-filter:blur(24px) saturate(1.3);border:1px solid var(--line);border-radius:14px;overflow:hidden;box-shadow:0 16px 44px rgba(0,0,0,.16),inset 0 1px 0 rgba(255,255,255,.035);transform-origin:50% 0;will-change:opacity,transform;animation:settings-shell-in .34s cubic-bezier(.16,1,.3,1) both}
.top{padding:13px 22px 0;background:var(--glass-bar);backdrop-filter:blur(18px) saturate(1.25);-webkit-backdrop-filter:blur(18px) saturate(1.25);border-bottom:1px solid var(--line)}
.topline{display:flex;align-items:center;justify-content:space-between;min-height:28px;margin-bottom:11px}
.topline strong{font-size:14px;line-height:1.35;font-weight:600;letter-spacing:-.1px}
.done{display:inline-block;text-decoration:none;border:1px solid transparent;border-radius:8px;padding:7px 13px;background:var(--selected);color:var(--text);font:inherit;line-height:1.35;cursor:pointer}
.categories{display:flex;align-items:stretch;justify-content:center;gap:2px;overflow-x:auto;padding:2px 0 8px;scrollbar-width:none}
.categories::-webkit-scrollbar{display:none}
.category{appearance:none;flex:1 1 0;min-width:58px;max-width:92px;border:1px solid transparent;border-radius:7px;background:transparent;color:var(--text);padding:4px 3px 5px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;cursor:pointer;font:inherit;line-height:1.3;transition:color .12s ease}
.category svg{width:24px;height:24px;padding:0;box-sizing:border-box;border-radius:0;stroke:currentColor;fill:none;stroke-width:1.55;stroke-linecap:round;stroke-linejoin:round;background:transparent;color:var(--muted)}
/* Preserve the distinct, restrained colors of each settings category. */
.categories .category:nth-child(1) svg{color:#858990}
.categories .category:nth-child(2) svg{color:#4285dc}
.categories .category:nth-child(3) svg{color:#27a4b8}
.categories .category:nth-child(4) svg{color:#d2a13c}
.categories .category:nth-child(5) svg{color:#6b8fd6}
.categories .category:nth-child(6) svg{color:#d69b38}
.categories .category:nth-child(7) svg{color:#9b7bd3}
.categories .category:nth-child(8) svg{color:#39a879}
.categories .category:nth-child(9) svg{color:#548fd7}
.categories .category:nth-child(10) svg{color:#a17ad5}
.categories .category:nth-child(11) svg{color:#858990}
.categories .category:nth-child(12) svg{color:#de8d52}
.categories .category:nth-child(13) svg{color:#5789db}
.category.active svg{filter:saturate(1.12)}
.category span{font-size:11px;line-height:1.25;white-space:nowrap;font-weight:500}
.category:hover{background:transparent;border-color:transparent;transform:none}
.category:active{transform:none}
.category:focus-visible{outline:2px solid var(--blue);outline-offset:1px;border-radius:7px}
.category.active{background:transparent;color:var(--text);box-shadow:none}
.checkrow input:disabled{opacity:.5;cursor:not-allowed}
.checkrow .muted{color:var(--muted);font-size:12px}
.content{max-width:790px;margin:0 auto;padding:30px 34px 34px}
h1{font-size:22px;line-height:1.25;font-weight:600;letter-spacing:-.35px;margin:0 0 6px}
.sub{color:var(--muted);font-size:13px;line-height:1.5;margin:0 0 27px}
@keyframes settings-shell-in{from{opacity:0;transform:translateY(14px) scale(.978)}70%{opacity:1;transform:translateY(-1px) scale(1.002)}to{opacity:1;transform:translateY(0) scale(1)}}@keyframes settings-panel-in{from{opacity:0;transform:translateY(7px)}to{opacity:1;transform:translateY(0)}}.panel{display:none}.panel.active{display:block;animation:settings-panel-in .26s cubic-bezier(.16,1,.3,1) both}
.field{display:grid;grid-template-columns:minmax(180px,230px) minmax(0,1fr);align-items:center;gap:18px;padding:15px 0;border-bottom:1px solid var(--line)}
.field:first-child{border-top:1px solid var(--line)}
.field label,.field .fieldname{font-size:13px;line-height:1.4;font-weight:500}
input[type=text],select{width:100%;min-width:0;min-height:36px;border:1px solid var(--line);border-radius:8px;background:var(--bar);color:var(--text);padding:7px 11px;font:inherit;font-size:13px;line-height:1.35;box-shadow:inset 0 1px 1px rgba(0,0,0,.025)}
input[type=text]:focus,select:focus{outline:none;border-color:var(--blue);box-shadow:0 0 0 3px rgba(10,132,255,.18)}
.checks{border:1px solid var(--line);border-radius:13px;overflow:hidden;margin-top:6px}
.checkrow{display:flex;align-items:center;gap:10px;padding:13px 14px;border-bottom:1px solid var(--line);font-size:13px}
.checkrow:last-child{border-bottom:0}
input[type=checkbox]{width:15px;height:15px;accent-color:var(--blue)}
.help{font-size:12px;color:var(--muted);line-height:1.55;margin:12px 0 0}
.actions{display:flex;gap:9px;align-items:center;flex-wrap:wrap;margin-top:22px}
.action{display:inline-block;text-decoration:none;border:1px solid var(--line);border-radius:8px;background:var(--bar);color:var(--text);padding:8px 12px;font:inherit;font-size:13px;line-height:1.35;cursor:pointer;transition:background .18s ease,border-color .18s ease,transform .18s cubic-bezier(.2,.7,.2,1),box-shadow .18s ease}
.action:hover,.done:hover{border-color:var(--muted);background:var(--selected);transform:translateY(-1px);box-shadow:0 4px 12px rgba(0,0,0,.08)}.action:active,.done:active{transform:translateY(0) scale(.985);box-shadow:none}
.image-actions{display:flex;align-items:center;gap:8px;flex-wrap:wrap;min-width:0}
.image-path{font-size:12px;color:var(--muted);overflow-wrap:anywhere}
.action.primary{border-color:var(--blue);background:var(--blue);color:white;font-weight:600}
.action.primary:hover{filter:brightness(1.04);box-shadow:0 5px 15px rgba(10,132,255,.18)}
.status{color:var(--muted);font-size:12px;margin-left:5px}
.about-cover{display:block;width:100%;aspect-ratio:3.15/1;max-height:270px;min-height:170px;object-fit:cover;object-position:center 46%;border:1px solid var(--line);border-radius:14px;background:var(--bar);margin:0 0 25px}
.about-copy h2{margin:0 0 8px;font-size:22px;line-height:1.3;font-weight:600;letter-spacing:-.4px}
.about-copy p{margin:0;color:var(--muted);font-size:13px;line-height:1.7;max-width:720px}
.about-badges{display:flex;flex-wrap:wrap;gap:8px;margin:18px 0 22px}
.about-badge{padding:6px 10px;border:1px solid var(--line);border-radius:999px;color:var(--muted);font-size:12px}
.about-version{border:1px solid var(--line);border-radius:12px;overflow:hidden;background:var(--panel)}
.about-version-row{display:flex;align-items:center;justify-content:space-between;gap:18px;padding:12px 14px;border-bottom:1px solid var(--line);font-size:13px}
.about-version-row:last-child{border-bottom:0}
.about-version-row span{color:var(--muted)}
.about-version-row strong{font-weight:500;text-align:right}
.about-links{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;padding-top:16px;margin-top:18px;border-top:1px solid var(--line);font-size:12px;color:var(--muted)}
.about-links a{color:var(--blue);text-decoration:none}.about-links a:hover{text-decoration:underline}
@media(max-width:720px){.shell{width:calc(100% - 16px);margin:0 auto 12px;border-radius:14px}.top{padding:10px 8px 0}.category{min-width:66px}.content{padding:24px 18px}.field{grid-template-columns:1fr;gap:8px}.about-cover{aspect-ratio:2.2/1;min-height:140px}.about-links{align-items:flex-start;flex-direction:column}.about-version-row{padding:11px}}@media(prefers-reduced-motion:reduce){*,*::before,*::after{animation-duration:.01ms!important;animation-iteration-count:1!important;transition-duration:.01ms!important;scroll-behavior:auto!important}}
</style></head><body>
<div class="shell"><form action="aurora-settings://save" method="get"><header class="top"><div class="topline"><strong>Aurora Settings</strong><a class="done" href="aurora-settings://close">Done</a></div>
<nav class="categories" aria-label="Settings categories">
<button class="category__ACTIVE_GENERAL__" type="submit" formaction="aurora-settings://navigate" name="section" value="0"><svg viewBox="0 0 24 24"><path d="M9.6 2.8h4.8l.6 2.3c.7.2 1.3.5 1.9.8l2.1-.8 2.4 4.1-1.6 1.7v2.1l1.6 1.7-2.4 4.1-2.1-.8c-.6.4-1.2.7-1.9.9l-.6 2.3H9.6L9 18.9c-.7-.2-1.3-.5-1.9-.9l-2.1.8-2.4-4.1 1.6-1.7v-2.1L2.6 9.2 5 5.1l2.1.8c.6-.3 1.2-.6 1.9-.8z"/><circle cx="12" cy="11.8" r="3.1"/></svg><span>General</span></button>
<button class="category__ACTIVE_TABS__" type="submit" formaction="aurora-settings://navigate" name="section" value="1"><svg viewBox="0 0 24 24"><path d="M7.2 6.2V4.8A1.8 1.8 0 0 1 9 3h10.2A1.8 1.8 0 0 1 21 4.8v11.1a1.8 1.8 0 0 1-1.8 1.8h-1.4"/><rect x="3" y="6.5" width="13.6" height="14.5" rx="2"/></svg><span>Tabs</span></button>
<button class="category__ACTIVE_AUTOFILL__" type="submit" formaction="aurora-settings://navigate" name="section" value="2"><svg viewBox="0 0 24 24"><rect x="3" y="4.5" width="18" height="15" rx="2"/><path d="M3.5 8h17"/><circle cx="8.1" cy="12" r="1.8"/><path d="M5.6 16c.3-1.5 4.7-1.5 5 0M13 12h4.5M13 15h4.5"/></svg><span>AutoFill</span></button>
<button class="category__ACTIVE_PASSWORDS__" type="submit" formaction="aurora-settings://navigate" name="section" value="3"><svg viewBox="0 0 24 24"><circle cx="7.4" cy="15.8" r="3.5"/><circle cx="7.4" cy="15.8" r=".85"/><path d="m10 13.3 8.7-8.7 2.1 2.1-2.2 2.2 1.8 1.8-2.2 2.2-1.8-1.8-3.7 3.7"/></svg><span>Passwords</span></button>
<button class="category__ACTIVE_SEARCH__" type="submit" formaction="aurora-settings://navigate" name="section" value="4"><svg viewBox="0 0 24 24"><circle cx="10.5" cy="10.5" r="6.4"/><path d="m15.2 15.2 5 5"/></svg><span>Search</span></button>
<button class="category__ACTIVE_SECURITY__" type="submit" formaction="aurora-settings://navigate" name="section" value="5"><svg viewBox="0 0 24 24"><rect x="4.7" y="10" width="14.6" height="10.7" rx="1.8"/><path d="M7.5 10V7.2a4.5 4.5 0 0 1 9 0V10"/><circle cx="12" cy="14.7" r="1"/><path d="M12 15.7v1.5"/></svg><span>Security</span></button>
<button class="category__ACTIVE_PRIVACY__" type="submit" formaction="aurora-settings://navigate" name="section" value="6"><svg viewBox="0 0 24 24"><path d="M6.5 11V5.4a1.5 1.5 0 0 1 3 0v5.2V4.2a1.5 1.5 0 0 1 3 0v6.4V5.1a1.5 1.5 0 0 1 3 0v5.5V7.5a1.5 1.5 0 0 1 3 0v6.1c0 4.1-2.5 6.7-6.3 6.7h-.7c-2.3 0-3.8-1.3-5-3.1l-2.1-3.1a1.5 1.5 0 0 1 2.4-1.8L8.8 14"/></svg><span>Privacy</span></button>
<button class="category__ACTIVE_WEBSITES__" type="submit" formaction="aurora-settings://navigate" name="section" value="7"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a15 15 0 0 1 0 18M12 3a15 15 0 0 0 0 18"/></svg><span>Websites</span></button>
<button class="category__ACTIVE_PROFILES__" type="submit" formaction="aurora-settings://navigate" name="section" value="8"><svg viewBox="0 0 24 24"><circle cx="8.8" cy="8" r="3.1"/><path d="M2.8 20c.2-3.5 2.5-5.5 6-5.5s5.8 2 6 5.5M15.5 5.3a2.8 2.8 0 0 1 0 5.4M16.1 14.8c2.8.5 4.4 2.4 4.7 5.2"/></svg><span>Profiles</span></button>
<button class="category__ACTIVE_EXTENSIONS__" type="submit" formaction="aurora-settings://navigate" name="section" value="9"><svg viewBox="0 0 24 24"><path d="M9 3.5H5.5A2 2 0 0 0 3.5 5.5V9h2a2.5 2.5 0 1 1 0 5h-2v4.5a2 2 0 0 0 2 2H10v-2a2.5 2.5 0 1 1 5 0v2h3.5a2 2 0 0 0 2-2V14h-2a2.5 2.5 0 1 1 0-5h2V5.5a2 2 0 0 0-2-2H14v2a2.5 2.5 0 1 1-5 0z"/></svg><span>Extensions</span></button>
<button class="category__ACTIVE_ADVANCED__" type="submit" formaction="aurora-settings://navigate" name="section" value="10"><svg viewBox="0 0 24 24"><path d="M9.6 2.8h4.8l.6 2.3c.7.2 1.3.5 1.9.8l2.1-.8 2.4 4.1-1.6 1.7v2.1l1.6 1.7-2.4 4.1-2.1-.8c-.6.4-1.2.7-1.9.9l-.6 2.3H9.6L9 18.9c-.7-.2-1.3-.5-1.9-.9l-2.1.8-2.4-4.1 1.6-1.7v-2.1L2.6 9.2 5 5.1l2.1.8c.6-.3 1.2-.6 1.9-.8z"/><circle cx="12" cy="11.8" r="3.1"/></svg><span>Advanced</span></button>
<button class="category__ACTIVE_APPEARANCE__" type="submit" formaction="aurora-settings://navigate" name="section" value="11"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2.5 12h2M19.5 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg><span>Appearance</span></button>
<button class="category__ACTIVE_ABOUT__" type="submit" formaction="aurora-settings://navigate" name="section" value="12"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 11v5"/><circle cx="12" cy="7.6" r=".65" fill="currentColor" stroke="none"/></svg><span>About</span></button>
</nav></header><main class="content">
<section class="panel__ACTIVE_SECTION_GENERAL__" data-section="general"><h1>General</h1><p class="sub">Choose your homepage and where downloads are saved.</p>
<div class="field"><label for="home">Homepage</label><input id="home" type="text" name="home" value="__HOME__"></div>
<div class="field"><label for="downloads">Downloads location</label><input id="downloads" type="text" name="downloads" value="__DOWNLOADS__"></div>
<div class="field"><label for="newWindowDestination">New windows open with</label><select id="newWindowDestination" name="newWindowsOpenWith"><option value="startpage"__NEW_WINDOW_START__>Start Page</option><option value="homepage"__NEW_WINDOW_HOME__>Homepage</option></select></div>
<div class="field"><label for="newTabDestination">New tabs open with</label><select id="newTabDestination" name="newTabsOpenWith"><option value="startpage"__NEW_TAB_START__>Start Page</option><option value="homepage"__NEW_TAB_HOME__>Homepage</option></select></div>
<p class="help">Choose what Aurora displays when a new window or tab opens. The homepage is the address above; Start Page shows Favorites and your selected Start Page sections.</p>
<div class="checks">
<label class="checkrow"><input type="checkbox" disabled>Remove history items automatically</label>
<label class="checkrow"><input type="checkbox" disabled>Open downloaded “safe” files automatically</label>
</div>
</section>
<section class="panel__ACTIVE_SECTION_TABS__" data-section="tabs"><h1>Tabs</h1><p class="sub">Control how links opened by websites behave.</p>
<div class="checks">
<label class="checkrow"><input type="hidden" name="backgroundTabs" value="0"><input id="backgroundTabs" type="checkbox" name="backgroundTabs" value="1"__BACKGROUND_TABS__>Open links in new tabs in the background</label>
<label class="checkrow"><input type="hidden" name="websiteIcons" value="0"><input id="websiteIcons" type="checkbox" name="websiteIcons" value="1"__WEBSITE_ICONS__>Show website icons in tabs</label>
<label class="checkrow"><input type="hidden" name="alwaysShowTabBar" value="0"><input id="alwaysShowTabBar" type="checkbox" name="alwaysShowTabBar" value="1"__ALWAYS_SHOW_TAB_BAR__>Always show the tab bar, even with one tab</label>
</div></section>
<section class="panel__ACTIVE_SECTION_ABOUT__" data-section="about">
 <img class="about-cover" src="__AURORA_ABOUT_COVER__" alt="Aurora — Browse Beyond">
 <div class="about-copy"><h2>About Aurora</h2><p>A fast, private and beautiful browser shell built with WebKit for Windows. Aurora pairs a custom native interface with the WebKit browser engine while keeping its own identity.</p></div>
 <div class="about-badges"><span class="about-badge">Windows</span><span class="about-badge">WebKit engine</span><span class="about-badge">Experimental project</span></div>
 <div class="about-version" aria-label="Aurora version information">
  <div class="about-version-row"><span>Version</span><strong>1.0.0</strong></div>
  <div class="about-version-row"><span>Browser engine</span><strong>WebKit</strong></div>
  <div class="about-version-row"><span>Platform</span><strong>Windows</strong></div>
 </div>
 <div class="about-links"><span>Powered by WebKit</span><a href="https://github.com/Takayuushi/Aurora-Webkit">Aurora on GitHub ↗</a></div>
</section>
<section class="panel__ACTIVE_SECTION_APPEARANCE__" data-section="appearance"><h1>Appearance</h1><p class="sub">Choose how Aurora looks, including the Start Page wallpaper.</p>
<div class="field"><label for="theme">Browser appearance</label><select id="theme" name="theme"><option value="system"__THEME_SYSTEM__>Automatic (follow Windows)</option><option value="light"__THEME_LIGHT__>Light</option><option value="dark"__THEME_DARK__>Dark</option></select></div>
<div class="field"><div class="fieldname">Start Page background</div><div class="image-actions"><span class="image-path">__START_PAGE_BACKGROUND__</span><a class="action" href="aurora-settings://browse-background">Choose Image…</a><a class="action" href="aurora-settings://clear-background">Use Default</a></div></div>
<p class="help">Choose a PNG, JPEG, WebP, GIF, or BMP image from this PC (up to 24 MB). Leave it unset to use Aurora’s built-in aurora wallpaper.</p>
<div class="checks" aria-label="Start Page sections">
<label class="checkrow"><input type="hidden" name="showFavorites" value="0"><input id="showFavorites" type="checkbox" name="showFavorites" value="1"__SHOW_FAVORITES__>Show Favorites</label>
<label class="checkrow"><input type="hidden" name="showFrequentlyVisited" value="0"><input id="showFrequentlyVisited" type="checkbox" name="showFrequentlyVisited" value="1"__SHOW_FREQUENTLY_VISITED__>Show Frequently Visited</label>
<label class="checkrow"><input type="hidden" name="showReadingList" value="0"><input id="showReadingList" type="checkbox" name="showReadingList" value="1"__SHOW_READING_LIST__>Show Reading List</label>
</div></section>
<section class="panel__ACTIVE_SECTION_AUTOFILL__" data-section="autofill"><h1>AutoFill</h1><p class="sub">Choose what Aurora may fill in website forms.</p>
<div class="checks">
<label class="checkrow"><input type="checkbox" disabled>Use information from contacts</label>
<label class="checkrow"><input type="checkbox" disabled>Use usernames and passwords</label>
<label class="checkrow"><input type="checkbox" disabled>Use credit cards</label>
<label class="checkrow"><input type="checkbox" disabled>Use other form information</label>
</div></section>
<section class="panel__ACTIVE_SECTION_PASSWORDS__" data-section="passwords"><h1>Passwords</h1><p class="sub">Manage saved passwords and passkeys.</p>
<div class="checks">
<label class="checkrow"><input type="checkbox" disabled>AutoFill passwords and passkeys</label>
<label class="checkrow"><input type="checkbox" disabled>Suggest strong passwords</label>
<label class="checkrow"><input type="checkbox" disabled>Detect compromised passwords</label>
</div></section>
<section class="panel__ACTIVE_SECTION_SEARCH__" data-section="search"><h1>Search</h1><p class="sub">Choose the search engine used by Smart Search.</p>
<div class="field"><label for="engine">Default search engine</label><select id="engine" name="engine"><option value="Google"__ENGINE_GOOGLE__>Google</option><option value="Bing"__ENGINE_BING__>Bing</option><option value="DuckDuckGo"__ENGINE_DUCK__>DuckDuckGo</option><option value="Yahoo"__ENGINE_YAHOO__>Yahoo</option><option value="Ecosia"__ENGINE_ECOSIA__>Ecosia</option></select></div>
<div class="checks"><label class="checkrow"><input type="hidden" name="searchSuggestions" value="0"><input id="searchSuggestions" type="checkbox" name="searchSuggestions" value="1"__SEARCH_SUGGESTIONS__>Show browsing history suggestions in Smart Search</label></div></section>
<section class="panel__ACTIVE_SECTION_SECURITY__" data-section="security"><h1>Security</h1><p class="sub">Review site security protections provided by the current engine.</p>
<div class="checks">
<label class="checkrow"><input type="checkbox" disabled>Warn when visiting fraudulent websites</label>
<label class="checkrow"><input type="checkbox" disabled>Use a built-in malicious-site database</label>
<label class="checkrow"><input type="hidden" name="javascript" value="0"><input id="javascript" type="checkbox" name="javascript" value="1"__JAVASCRIPT__>Enable JavaScript</label>
</div></section>
<section class="panel__ACTIVE_SECTION_WEBSITES__" data-section="websites"><h1>Websites</h1><p class="sub">Choose the default permissions used for websites.</p>
<div class="checks">
<label class="checkrow"><input type="hidden" name="images" value="0"><input id="images" type="checkbox" name="images" value="1"__IMAGES__>Load images automatically</label>
<label class="checkrow"><input type="hidden" name="popups" value="0"><input id="popups" type="checkbox" name="popups" value="1"__POPUPS__>Allow pop-up windows</label>
<label class="checkrow"><input type="hidden" name="storage" value="0"><input id="storage" type="checkbox" name="storage" value="1"__STORAGE__>Allow website storage</label></div></section>
<section class="panel__ACTIVE_SECTION_PRIVACY__" data-section="privacy"><h1>Privacy</h1><p class="sub">Manage the local data Aurora stores while you browse.</p>
<div class="checks">
<label class="checkrow"><input type="checkbox" disabled>Prevent cross-site tracking</label>
<label class="checkrow"><input type="checkbox" disabled>Hide IP address from trackers</label>
<label class="checkrow"><input type="checkbox" disabled>Block all cookies</label>
</div><div class="actions"><a class="action" href="aurora-settings://clear-history">Clear History…</a><a class="action" href="aurora-settings://clear-data">Manage / Clear Website Data…</a></div>
<p class="help">Clearing website data may sign you out of websites.</p></section>
<section class="panel__ACTIVE_SECTION_PROFILES__" data-section="profiles"><h1>Profiles</h1><p class="sub">Keep browsing data separated for different uses.</p>
<div class="checks"><label class="checkrow"><input type="checkbox" disabled>Use separate profiles for work and personal browsing</label><label class="checkrow"><input type="checkbox" disabled>Choose a profile when a new window opens</label></div>
</section>
<section class="panel__ACTIVE_SECTION_EXTENSIONS__" data-section="extensions"><h1>Extensions</h1><p class="sub">Review user scripts installed for Aurora.</p>
<div class="checks">__EXTENSION_ITEMS__</div>
</section>
<section class="panel__ACTIVE_SECTION_ADVANCED__" data-section="advanced"><h1>Advanced</h1><p class="sub">Options for development and diagnostics.</p>
<div class="checks">
<label class="checkrow"><input type="hidden" name="developer" value="0"><input id="developer" type="checkbox" name="developer" value="1"__DEVELOPER__>Enable Web Inspector / developer tools</label>
<label class="checkrow"><input type="hidden" name="fullWebsiteAddress" value="0"><input id="fullWebsiteAddress" type="checkbox" name="fullWebsiteAddress" value="1"__FULL_WEBSITE_ADDRESS__>Show full website addresses in the Smart Search field</label>
</div>
</section>
<div class="actions"><button class="action primary" type="submit">Save Changes</button></div>
</main>
<script>
(() => {
    const sectionNames = ['general', 'tabs', 'autofill', 'passwords', 'search', 'security', 'privacy', 'websites', 'profiles', 'extensions', 'advanced', 'appearance', 'about'];
    const categories = Array.from(document.querySelectorAll('.categories .category'));
    const panels = Array.from(document.querySelectorAll('.panel'));
    categories.forEach((button, index) => {
        button.addEventListener('click', event => {
            event.preventDefault();
            const sectionName = sectionNames[index];
            if (!sectionName)
                return;
            categories.forEach((item, itemIndex) => item.classList.toggle('active', itemIndex === index));
            panels.forEach(panel => panel.classList.toggle('active', panel.dataset.section === sectionName));
            window.location.href = 'aurora-settings://navigate?section=' + index;
        });
    });
})();
</script>
</form></div></body></html>)HTML";

        const std::pair<const std::string, const std::string> replacements[] = {
            { "__HOME__", home }, { "__DOWNLOADS__", downloadsPath },
            { "__START_PAGE_BACKGROUND__", startPageBackgroundLabel },
            { "__THEME_SYSTEM__", systemSelected }, { "__THEME_LIGHT__", lightSelected }, { "__THEME_DARK__", darkSelected },
            { "__ENGINE_GOOGLE__", googleSelected }, { "__ENGINE_BING__", bingSelected }, { "__ENGINE_DUCK__", duckSelected },
            { "__ENGINE_YAHOO__", yahooSelected }, { "__ENGINE_ECOSIA__", ecosiaSelected },
            { "__NEW_WINDOW_START__", newWindowStartSelected }, { "__NEW_WINDOW_HOME__", newWindowHomeSelected },
            { "__NEW_TAB_START__", newTabStartSelected }, { "__NEW_TAB_HOME__", newTabHomeSelected },
            { "__WEBSITE_ICONS__", checked(showWebsiteIconsInTabs) },
            { "__ALWAYS_SHOW_TAB_BAR__", checked(alwaysShowTabBar) },
            { "__JAVASCRIPT__", checked(javascriptEnabled) }, { "__IMAGES__", checked(imagesEnabled) },
            { "__POPUPS__", checked(allowPopups) }, { "__STORAGE__", checked(localStorageEnabled) },
            { "__DEVELOPER__", checked(developerToolsEnabled) },
            { "__SEARCH_SUGGESTIONS__", checked(searchSuggestionsEnabled) },
            { "__FULL_WEBSITE_ADDRESS__", checked(showFullWebsiteAddress) },
            { "__EXTENSION_ITEMS__", extensionItems },
            { "__BACKGROUND_TABS__", checked(openLinksInBackground) },
            { "__SHOW_FAVORITES__", checked(showStartPageFavorites) },
            { "__SHOW_FREQUENTLY_VISITED__", checked(showStartPageFrequentlyVisited) },
            { "__SHOW_READING_LIST__", checked(showStartPageReadingList) },
            { "__ACTIVE_GENERAL__", activeGeneral }, { "__ACTIVE_TABS__", activeTabs },
            { "__ACTIVE_APPEARANCE__", activeAppearance }, { "__ACTIVE_AUTOFILL__", activeAutoFill },
            { "__ACTIVE_PASSWORDS__", activePasswords }, { "__ACTIVE_SEARCH__", activeSearch },
            { "__ACTIVE_SECURITY__", activeSecurity }, { "__ACTIVE_WEBSITES__", activeWebsites },
            { "__ACTIVE_PRIVACY__", activePrivacy }, { "__ACTIVE_PROFILES__", activeProfiles },
            { "__ACTIVE_EXTENSIONS__", activeExtensions }, { "__ACTIVE_ADVANCED__", activeAdvanced },
            { "__ACTIVE_SECTION_GENERAL__", activeGeneral }, { "__ACTIVE_SECTION_TABS__", activeTabs },
            { "__ACTIVE_SECTION_APPEARANCE__", activeAppearance }, { "__ACTIVE_SECTION_AUTOFILL__", activeAutoFill },
            { "__ACTIVE_SECTION_PASSWORDS__", activePasswords }, { "__ACTIVE_SECTION_SEARCH__", activeSearch },
            { "__ACTIVE_SECTION_SECURITY__", activeSecurity }, { "__ACTIVE_SECTION_WEBSITES__", activeWebsites },
            { "__ACTIVE_SECTION_PRIVACY__", activePrivacy }, { "__ACTIVE_SECTION_PROFILES__", activeProfiles },
            { "__ACTIVE_SECTION_EXTENSIONS__", activeExtensions }, { "__ACTIVE_SECTION_ADVANCED__", activeAdvanced },
            { "__ACTIVE_ABOUT__", activeAbout }, { "__ACTIVE_SECTION_ABOUT__", activeAbout },
            { "__AURORA_ABOUT_COVER__", imageDataUrl(L"aurora-about-cover.jpg", "image/jpeg") }
        };
        for (const auto& replacement : replacements) {
            size_t offset = 0;
            while ((offset = html.find(replacement.first, offset)) != std::string::npos) {
                html.replace(offset, replacement.first.size(), replacement.second);
                offset += replacement.second.size();
            }
        }
        loadLocalHTML(tab, html);
        tab.activeUrl = L"about:settings";
        tab.title = L"Settings";
        tab.lastRecordedUrl.clear();
        SetWindowTextW(addressBar, tab.activeUrl.c_str());
        const std::wstring caption = tab.title + L" — Aurora";
        SetWindowTextW(window, caption.c_str());
    }



    void loadStartPage(TabState& tab)
    {
        tab.initialPageLoadPending = false;
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
:root{font-family:-apple-system,BlinkMacSystemFont,"Inter","Segoe UI Variable Text","Segoe UI",Arial,sans-serif;color:#f7f9ff;--muted:rgba(245,248,255,.76)}
*{box-sizing:border-box}
html,body{margin:0;min-height:100%;width:100%}
body{min-height:100vh;background:#10243d;color:#f7f9ff;overflow-x:hidden;position:relative}
.wallpaper{position:fixed;inset:0;z-index:0;background:
linear-gradient(180deg,rgba(4,12,26,.04) 0%,rgba(4,12,26,.14) 38%,rgba(4,10,22,.72) 100%),
radial-gradient(ellipse at 27% 13%,rgba(18,223,176,.68) 0%,rgba(18,223,176,0) 36%),
radial-gradient(ellipse at 72% 10%,rgba(141,96,255,.68) 0%,rgba(141,96,255,0) 35%),
radial-gradient(ellipse at 52% 43%,rgba(44,151,207,.36) 0%,rgba(44,151,207,0) 42%),
linear-gradient(135deg,#081a32 0%,#17425b 49%,#28234f 100%);background-size:cover;background-position:center}
.wallpaper:before{content:"";position:absolute;left:-3%;right:-3%;bottom:0;height:48%;opacity:.78;background:linear-gradient(180deg,rgba(27,76,96,.42),rgba(4,13,27,.96));clip-path:polygon(0 37%,7% 27%,14% 43%,22% 12%,31% 36%,39% 25%,48% 48%,57% 14%,65% 35%,74% 8%,83% 40%,91% 21%,100% 38%,100% 100%,0 100%)}
.wallpaper:after{content:"";position:absolute;left:0;right:0;bottom:0;height:24%;opacity:.78;background:repeating-linear-gradient(176deg,transparent 0,transparent 13px,rgba(92,198,219,.10) 14px,transparent 16px),linear-gradient(180deg,rgba(18,71,105,.24),rgba(3,9,22,.82))}
.page{position:relative;transform:translate3d(0,0,0);z-index:1;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:34px 24px 42px;transition:transform .24s cubic-bezier(.16,1,.3,1);animation:start-page-fade .38s cubic-bezier(.2,.75,.25,1) both}
.branding{text-align:center;margin-bottom:25px;text-shadow:0 2px 20px rgba(0,0,0,.2);animation:start-page-enter .42s cubic-bezier(.2,.75,.25,1) both}
.brand-mark{width:80px;height:80px;margin:0 auto 10px;border:0;border-radius:0;background:transparent;box-shadow:none;display:grid;place-items:center}
.brand-mark img{width:80px;height:80px;display:block;object-fit:contain;filter:drop-shadow(0 10px 24px rgba(0,0,0,.22))}
.branding h1{font-size:35px;font-weight:600;letter-spacing:-1.2px;line-height:1.1;margin:0}
.branding p{font-size:10px;font-weight:500;letter-spacing:4px;margin:8px 0 0;color:rgba(248,251,255,.82)}
.search-wrap{width:min(690px,90vw);position:relative;margin:0 auto 34px}
.search{width:100%;height:50px;margin:0;padding:0 20px 0 52px;border:1px solid rgba(255,255,255,.38);border-radius:15px;background:rgba(248,250,255,.16);backdrop-filter:blur(20px) saturate(1.25);-webkit-backdrop-filter:blur(20px) saturate(1.25);color:#fff;outline:none;font-size:15px;box-shadow:0 12px 32px rgba(0,0,0,.12),inset 0 1px 0 rgba(255,255,255,.08);transition:background .22s ease,border-color .22s ease,box-shadow .22s ease,transform .22s cubic-bezier(.2,.7,.2,1)}
.search:hover{background:rgba(248,250,255,.19);border-color:rgba(255,255,255,.5)}.search:focus{border-color:rgba(255,255,255,.72);box-shadow:0 0 0 3px rgba(255,255,255,.14),0 12px 32px rgba(0,0,0,.14);transform:translateY(-1px)}
.search::placeholder{color:rgba(246,249,255,.72)}
.search-icon{position:absolute;left:20px;top:50%;width:17px;height:17px;transform:translateY(-50%);stroke:rgba(248,251,255,.84);fill:none;stroke-width:1.8;stroke-linecap:round;pointer-events:none}
.section{width:min(850px,94vw);text-align:left;margin-bottom:13px;color:rgba(249,251,255,.9);font-size:13px;font-weight:600;text-shadow:0 1px 9px rgba(0,0,0,.32)}
.section-spaced{margin-top:22px}
.tiles{width:min(850px,94vw);display:flex;justify-content:flex-start;align-items:flex-start;flex-wrap:wrap;gap:7px 10px}
.tile{width:96px;min-height:100px;padding:7px 3px 9px;border-radius:13px;border:1px solid transparent;background:transparent;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;gap:9px;color:#fff;cursor:pointer;transition:background .2s ease,border-color .2s ease,transform .2s cubic-bezier(.2,.7,.2,1),box-shadow .2s ease}
.tile:hover{background:rgba(255,255,255,.12);border-color:rgba(255,255,255,.18);transform:translateY(-2px) scale(1.015);box-shadow:0 8px 22px rgba(0,0,0,.08)}.tile:active{transform:translateY(0) scale(.975)}
.icon{width:54px;height:54px;flex:0 0 54px;border-radius:14px;display:grid;place-items:center;background:linear-gradient(145deg,rgba(255,255,255,.22),rgba(255,255,255,.08));border:1px solid rgba(255,255,255,.24);box-shadow:0 8px 22px rgba(0,0,0,.13),inset 0 1px 0 rgba(255,255,255,.13);color:#fff;font-weight:600;font-size:24px}
.name{font-size:12px;line-height:1.35;font-weight:500;max-width:88px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;text-align:center}
.host{display:none}
.reading-list{width:min(850px,94vw);display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:9px}
.reading-item{min-width:0;display:flex;flex-direction:column;gap:4px;padding:12px 15px;border:1px solid rgba(255,255,255,.2);border-radius:12px;background:rgba(255,255,255,.1);backdrop-filter:blur(16px) saturate(1.2);-webkit-backdrop-filter:blur(16px) saturate(1.2);color:#fff;text-decoration:none;transition:background .2s ease,border-color .2s ease,transform .2s cubic-bezier(.2,.7,.2,1),box-shadow .2s ease}
.reading-item:hover{background:rgba(255,255,255,.16);border-color:rgba(255,255,255,.32);transform:translateY(-1px);box-shadow:0 8px 22px rgba(0,0,0,.1)}
.reading-name{font-size:12px;line-height:1.4;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.reading-url{font-size:11px;color:rgba(245,248,255,.68);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.footer{margin-top:22px;color:rgba(245,248,255,.65);font-size:12px;text-align:center;text-shadow:0 1px 8px rgba(0,0,0,.3)}
.footer a{color:rgba(245,248,255,.82);text-decoration:none;margin-left:8px}
.footer a:hover{color:#fff;text-decoration:underline}
@keyframes start-page-enter{from{opacity:0;transform:translateY(7px)}to{opacity:1;transform:translateY(0)}}@keyframes start-page-fade{from{opacity:0}to{opacity:1}}@media(max-width:700px){.tile{width:calc(50% - 6px)}.page{padding-top:28px}.tiles{justify-content:center}.branding{margin-bottom:22px}}@media(prefers-reduced-motion:reduce){*,*::before,*::after{animation-duration:.01ms!important;animation-iteration-count:1!important;transition-duration:.01ms!important;scroll-behavior:auto!important}}</style>
</head>
<body>
<div class="wallpaper" id="wallpaper" style="__AURORA_BACKGROUND_IMAGE__"></div>
<div class="page">
<div class="branding">
<div class="brand-mark"><img src="__AURORA_LOGO_IMAGE__" alt="Aurora logo"></div>
<h1>Aurora</h1><p>BROWSE BEYOND</p>
</div>
<div class="search-wrap"><svg class="search-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.8" cy="10.8" r="6.6"/><path d="m16 16 4.2 4.2"/></svg><input id="search" class="search" autofocus placeholder="Search the web or enter an address"></div>)HTML";

        if (showStartPageFavorites)
            html += R"HTML(<div class="section">Favorites</div><div class="tiles">)HTML";

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

            std::wstring lowerHost = host;
            if (!lowerHost.empty())
                CharLowerBuffW(lowerHost.data(), static_cast<DWORD>(lowerHost.size()));
            std::string iconBackground = "linear-gradient(145deg,#36cbb8,#287ca3)";
            std::string iconForeground = "#ffffff";
            if (lowerHost.find(L"youtube") != std::wstring::npos) iconBackground = "linear-gradient(145deg,#ff6961,#d81728)";
            else if (lowerHost.find(L"github") != std::wstring::npos) iconBackground = "linear-gradient(145deg,#50545b,#202124)";
            else if (lowerHost.find(L"reddit") != std::wstring::npos) iconBackground = "linear-gradient(145deg,#ff9850,#e54a16)";
            else if (lowerHost.find(L"google") != std::wstring::npos) { iconBackground = "linear-gradient(145deg,#ffffff,#eaf1ff)"; iconForeground = "#4285f4"; }
            else if (lowerHost.find(L"wikipedia") != std::wstring::npos) { iconBackground = "linear-gradient(145deg,#ffffff,#e7e9ed)"; iconForeground = "#34383e"; }
            else if (lowerHost.find(L"duckduckgo") != std::wstring::npos) iconBackground = "linear-gradient(145deg,#ffac54,#dd690d)";
            else if (lowerHost.find(L"microsoft") != std::wstring::npos) iconBackground = "linear-gradient(145deg,#5aa9ff,#6556ce)";
            else if (lowerHost.find(L"apple") != std::wstring::npos) { iconBackground = "linear-gradient(145deg,#f6f7f9,#d4d7dd)"; iconForeground = "#34383e"; }

            html += "<div class=\"tile\" onclick=\"location.href='";
            html += safeUrl;
            html += "'\"><div class=\"icon\" style=\"background:";
            html += iconBackground;
            html += ";color:";
            html += iconForeground;
            html += "\">";
            html += letter;
            html += "</div><div class=\"name\">";
            html += safeName;
            html += "</div><div class=\"host\">";
            html += safeHost;
            html += "</div></div>";
        };

        std::vector<SavedPage> startPageTiles;
        auto appendUniqueTile = [&](const std::wstring& url, const std::wstring& title) {
            if (url.empty() || startPageTiles.size() >= 8)
                return;
            const auto duplicate = std::find_if(startPageTiles.begin(), startPageTiles.end(),
                [&](const SavedPage& item) { return item.url == url; });
            if (duplicate == startPageTiles.end())
                startPageTiles.push_back({ url, title.empty() ? url : title, 0 });
        };
        for (const auto& entry : favorites)
            appendUniqueTile(entry.url, entry.title);

        // Keep the Favorites grid populated even when there are no bookmarks,
        // or an older bookmark entry doesn't contain a usable URL.
        const std::vector<SavedPage> starterFavorites {
            { L"https://www.youtube.com", L"YouTube", 0 },
            { L"https://github.com", L"GitHub", 0 },
            { L"https://www.reddit.com", L"Reddit", 0 },
            { L"https://www.wikipedia.org", L"Wikipedia", 0 },
            { L"https://www.google.com", L"Google", 0 },
            { L"https://duckduckgo.com", L"DuckDuckGo", 0 },
            { L"https://www.microsoft.com", L"Microsoft", 0 },
            { L"https://www.apple.com", L"Apple", 0 }
        };
        for (const auto& entry : starterFavorites)
            appendUniqueTile(entry.url, entry.title);

        if (showStartPageFavorites) {
            for (const auto& entry : startPageTiles) {
                std::wstring host = entry.url;
                const size_t scheme = host.find(L"://");
                if (scheme != std::wstring::npos)
                    host.erase(0, scheme + 3);
                const size_t slash = host.find_first_of(L"/?#");
                if (slash != std::wstring::npos)
                    host.erase(slash);
                addTile(entry.title.empty() ? host : entry.title, entry.url, host);
            }

            html += R"HTML(
</div>
)HTML";
        }
        std::vector<VisitEntry> recentTiles;
        for (const auto& entry : frequent) {
            if (entry.url.empty())
                continue;
            const auto duplicate = std::find_if(startPageTiles.begin(), startPageTiles.end(),
                [&](const SavedPage& item) { return item.url == entry.url; });
            if (!showStartPageFavorites || duplicate == startPageTiles.end())
                recentTiles.push_back(entry);
            if (recentTiles.size() >= 6)
                break;
        }
        if (showStartPageFrequentlyVisited && !recentTiles.empty()) {
            html += R"HTML(<div class="section section-spaced">Frequently Visited</div><div class="tiles">)HTML";
            for (const auto& entry : recentTiles) {
                std::wstring host = entry.url;
                const size_t scheme = host.find(L"://");
                if (scheme != std::wstring::npos)
                    host.erase(0, scheme + 3);
                const size_t slash = host.find_first_of(L"/?#");
                if (slash != std::wstring::npos)
                    host.erase(slash);
                addTile(entry.title.empty() ? host : entry.title, entry.url, host);
            }
            html += "</div>";
        }
        if (showStartPageReadingList && !readingList.empty()) {
            html += R"HTML(<div class="section section-spaced">Reading List</div><div class="reading-list">)HTML";
            size_t shownReadingItems = 0;
            for (auto entry = readingList.rbegin(); entry != readingList.rend() && shownReadingItems < 4; ++entry, ++shownReadingItems) {
                const std::wstring displayName = entry->title.empty() ? entry->url : entry->title;
                std::wstring host = entry->url;
                const size_t scheme = host.find(L"://");
                if (scheme != std::wstring::npos)
                    host.erase(0, scheme + 3);
                const size_t slash = host.find_first_of(L"/?#");
                if (slash != std::wstring::npos)
                    host.erase(slash);
                html += "<a class=\"reading-item\" href=\"";
                html += toUTF8(htmlEscape(entry->url));
                html += "\"><span class=\"reading-name\">";
                html += toUTF8(htmlEscape(displayName));
                html += "</span><span class=\"reading-url\">";
                html += toUTF8(htmlEscape(host));
                html += "</span></a>";
            }
            html += "</div>";
        }

        html += R"HTML(
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

        const std::string logoPlaceholder = "__AURORA_LOGO_IMAGE__";
        const size_t logoAt = html.find(logoPlaceholder);
        if (logoAt != std::string::npos) {
            const std::string logoData = auroraLogoDataUrl();
            html.replace(logoAt, logoPlaceholder.size(), logoData);
        }
        const std::string backgroundDataUrl = startPageBackgroundDataUrl();
        const std::string backgroundPlaceholder = "__AURORA_BACKGROUND_IMAGE__";
        const size_t backgroundPlaceholderPosition = html.find(backgroundPlaceholder);
        if (backgroundPlaceholderPosition != std::string::npos) {
            std::string backgroundStyle;
            if (!backgroundDataUrl.empty()) {
                backgroundStyle = "background-image:linear-gradient(180deg,rgba(4,12,26,.12) 0%,rgba(4,10,22,.22) 40%,rgba(4,10,22,.78) 100%),url('"
                    + backgroundDataUrl + "');";
            }
            html.replace(backgroundPlaceholderPosition, backgroundPlaceholder.size(), backgroundStyle);
        }
        loadLocalHTML(tab, html);

        tab.title = L"New Tab";
        tab.activeUrl.clear();
        tab.lastRecordedUrl.clear();
        if (active() == &tab) {
            SetWindowTextW(addressBar, L"");
            SetWindowTextW(window, windowTitle);
        }
    }

    void refreshOpenStartPages()
    {
        if (tabs.empty())
            return;
        const size_t previousActive = activeTab;
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (tabs[i]->internalPage != L"start")
                continue;
            activeTab = i;
            loadStartPage(*tabs[i]);
        }
        activeTab = std::min(previousActive, tabs.size() - 1);
        if (auto* selected = active()) {
            const wchar_t* address = selected->activeUrl.empty() || selected->activeUrl == L"about:blank"
                ? L"" : selected->activeUrl.c_str();
            SetWindowTextW(addressBar, address);
            const std::wstring titleText = (selected->title.empty() ? L"New Tab" : selected->title) + L" — Aurora";
            SetWindowTextW(window, titleText.c_str());
            invalidateTabChrome(selected);
        }
        resize();
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }

    void loadURL(TabState& tab, const std::wstring& value)
    {
        tab.initialPageLoadPending = false;
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

    bool addTab(bool select, bool loadInitialStartPage = true)
    {
        auto tab = std::make_unique<TabState>();
        tab->browser = this;

        RECT initialRect { };
        tab->view = adoptWK(WKViewCreate(initialRect, pageConfiguration.get(), window));
        if (!tab->view)
            return false;

        // WKViewCreate starts with a zero-sized child HWND. Keep inactive views
        // visible but at the bottom of the sibling z-order instead of hiding them:
        // Windows sends WM_SHOWWINDOW to WebKit, which marks the page non-visible and
        // can release a video renderer that is not correctly restored on tab return.
        HWND newViewWindow = WKViewGetWindow(tab->view.get());
        if (newViewWindow)
            SetWindowPos(newViewWindow, HWND_BOTTOM, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);

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

        WKPageUIClientV13 uiClient { };
        uiClient.base.version = 13;
        uiClient.base.clientInfo = this;
        uiClient.createNewPage = createNewTabForWebKit;
        WKPageSetPageUIClient(page, &uiClient.base);

        WKPageContextMenuClientV2 contextMenuClient { };
        contextMenuClient.base.version = 2;
        contextMenuClient.base.clientInfo = this;
        contextMenuClient.getContextMenuFromProposedMenu = getContextMenuFromProposedMenu;
        contextMenuClient.customContextMenuItemSelected = customContextMenuItemSelected;
        WKPageSetPageContextMenuClient(page, &contextMenuClient.base);

        WKPageSetCustomBackingScaleFactor(page, windowScaleFactor(window));

        // The live WebKit surface is not captured during ordinary tab switches.

        tabs.push_back(std::move(tab));
        size_t index = tabs.size() - 1;

        if (loadInitialStartPage) {
            const bool useHomepage = hasCreatedInitialTab
                ? newTabsOpenWithHomepage : newWindowsOpenWithHomepage;
            hasCreatedInitialTab = true;
            tabs[index]->initialPageLoadPending = true;
            tabs[index]->initialPageShouldLoadHomepage = useHomepage;
            if (useHomepage) {
                tabs[index]->internalPage.clear();
                tabs[index]->hasInternalThemeSnapshot = false;
            }
        } else {
            tabs[index]->title = L"New Tab";
            tabs[index]->activeUrl.clear();
            tabs[index]->lastRecordedUrl.clear();
            tabs[index]->internalPage.clear();
            tabs[index]->hasInternalThemeSnapshot = false;
        }
        if (select)
            activateTab(index);
        else
            resize();

        if (loadInitialStartPage) {
            // Let the tab chrome paint before building/loading all Start Page content.
            if (!window || !SetTimer(window, kDeferredTabLoadTimerId, 16, nullptr))
                loadPendingInitialTabPages();
        }
        return true;
    }

    void loadPendingInitialTabPages()
    {
        if (tabs.empty())
            return;

        // Initialize one page per UI tick instead of loading every queued start page
        // in a single timer callback. Prefer the visible tab so it becomes responsive first.
        size_t index = std::min(activeTab, tabs.size() - 1);
        if (!tabs[index]->initialPageLoadPending) {
            const auto pending = std::find_if(tabs.begin(), tabs.end(),
                [](const std::unique_ptr<TabState>& candidate) {
                    return candidate->initialPageLoadPending;
                });
            if (pending == tabs.end())
                return;
            index = static_cast<size_t>(std::distance(tabs.begin(), pending));
        }

        TabState& tab = *tabs[index];
        const bool useHomepage = tab.initialPageShouldLoadHomepage;
        tab.initialPageLoadPending = false;
        if (useHomepage)
            loadURL(tab, homePage);
        else
            loadStartPage(tab);

        if (auto* selected = active()) {
            const wchar_t* address = selected->activeUrl.empty() || selected->activeUrl == L"about:blank"
                ? L"" : selected->activeUrl.c_str();
            SetWindowTextW(addressBar, address);
            const std::wstring caption = (selected->title.empty() ? L"New Tab" : selected->title)
                + L" — Aurora";
            SetWindowTextW(window, caption.c_str());
            invalidateTabChrome(selected);
        }

        const bool morePending = std::any_of(tabs.begin(), tabs.end(),
            [](const std::unique_ptr<TabState>& candidate) {
                return candidate->initialPageLoadPending;
            });
        if (morePending && window)
            SetTimer(window, kDeferredTabLoadTimerId, 48, nullptr);
    }

    void invalidateTabChrome(TabState* tab)
    {
        if (!window || !tab)
            return;
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (tabs[i].get() != tab)
                continue;
            if (tabsBarHeight() > 0) {
                const RECT tabRect = tabBarItemRect(i);
                InvalidateRect(window, &tabRect, FALSE);
            }
            break;
        }
        if (active() == tab) {
            const RECT address = addressPillRect();
            InvalidateRect(window, &address, FALSE);
        }
    }

    void activateTab(size_t index)
    {
        if (index >= tabs.size())
            return;

        const size_t previouslyActive = activeTab;
        const bool switchingFromOverview = tabOverviewVisible;
        // Preserve the current animation position if a tab is selected while the
        // overview is still opening. Resetting this to zero causes a visible jump
        // back to the card before the selected-tab zoom can continue.
        const float overviewZoom = switchingFromOverview && tabOverviewAnimating
            ? tabOverviewZoomProgress() : 0.0f;
        if (switchingFromOverview && index == previouslyActive) {
            const float currentZoom = overviewZoom;
            tabOverviewClosingTab = static_cast<int>(index);
            // The active page has remained live behind the overview overlay.
            // Run the zoom back to the page, reversing smoothly if entry is in flight.
            tabOverviewZoomFrom = currentZoom;
            tabOverviewZoomTo = 1.0f;
            tabOverviewTransitionDuration = tabOverviewDurationForDistance(currentZoom, 1.0f);
            tabOverviewOpeningFramesRendered = 0;
            tabOverviewClosingFramesRendered = 0;
            tabOverviewVisible = false;
            tabOverviewAnimating = false;
            tabOverviewClosing = true;
            tabOverviewClosingSettling = false;
            tabOverviewClosingSettleFramesRendered = 0;
            tabOverviewAnimationFrameReady = false;
            tabOverviewClosingStart = GetTickCount64();
            
            startChromeAnimationTimer();
            hoveredOverviewTab = -1;
            hoveredOverviewNewTab = false;
            positionOverlayWindows(true);
            updateActiveStartPageSidebarOffset();
            if (auto* selected = active()) {
                if (selected->thumbnailCaptureNeeded || selected->previewURL != selected->activeUrl)
                    scheduleTabThumbnailCapture(selected, 420);
            }
            // Keep focus on the browser while the overview snapshot animates away.
            // Focusing WebKit here can synchronously wake its renderer mid-transition.
            if (auto* selected = active())
                invalidateTabChrome(selected);
            return;
        }
        if (addressEditing)
            endAddressEditing();
        if (!switchingFromOverview && previouslyActive != index && previouslyActive < tabs.size())
            captureTabThumbnail(*tabs[previouslyActive], true);
        if (!switchingFromOverview) {
            tabOverviewVisible = false;
            tabOverviewAnimating = false;
            tabOverviewClosing = false;
        } else {
            // Leave the overview overlay above the content while the selected
            // WebKit view is restored and synchronously painted underneath it.
            tabOverviewAnimating = false;
            tabOverviewClosing = false;
            if (tabOverviewWindow && IsWindow(tabOverviewWindow))
                InvalidateRect(tabOverviewWindow, nullptr, FALSE);
        }
        hoveredOverviewTab = -1;
        hoveredOverviewNewTab = false;
        activeTab = index;
        previousActiveTab = previouslyActive;
        tabSwitchAnimationStart = GetTickCount64();
        tabSwitchAnimating = previouslyActive != index && tabsBarHeight() > 0;
        if (tabSwitchAnimating)
            startChromeAnimationTimer();

        // Leave inactive WKViews at their existing z-order. They already sit
        // behind the active child, and reordering every overlapping surface on each
        // switch can expose intermediate black/white frames before the selected view
        // is raised. Only the selected child is brought forward below.

        auto* tab = active();
        if (!tab)
            return;

        updateActiveStartPageSidebarOffset();

        if (tab->activeUrl.empty() || tab->activeUrl == L"about:blank")
            SetWindowTextW(addressBar, L"");
        else
            SetWindowTextW(addressBar, tab->activeUrl.c_str());

        HWND selectedView = WKViewGetWindow(tab->view.get());
        // Switching tabs does not change toolbar geometry. Keep WebKit view
        // geometry stable and reorder only the selected surface once.
        resize(false);
        if (selectedView) {
            if (!IsWindowVisible(selectedView))
                ShowWindow(selectedView, SW_SHOWNA);
            if (!switchingFromOverview) {
                SetWindowPos(selectedView, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_SHOWWINDOW);
                // Request a normal asynchronous repaint for an already-open page.
                // Avoid RDW_UPDATENOW here so tab switching never blocks on WebKit.
                if (!tab->initialPageLoadPending) {
                    RedrawWindow(selectedView, nullptr, nullptr,
                        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
                }
            }
            // Keep the overview in control of focus until its zoom reaches the page.
            // The live WebKit view is raised underneath the overlay and focused only
            // after the final frame, avoiding a focus-induced frame stall.
            if (!switchingFromOverview) {
                // Keep new-tab creation responsive: don't synchronously wake a fresh,
                // still-uninitialized WebKit surface before its deferred first load.
                if (tab->initialPageLoadPending)
                    SetFocus(window);
                else
                    SetFocus(selectedView);
            }
        }

        if (switchingFromOverview) {
            // Put the selected live page at the front of the WebKit stack before the
            // animation finishes, but keep the overview HWND above it for the whole
            // transition. That lets WebKit settle its backing surface while covered.
            tabOverviewClosingTab = static_cast<int>(index);
            tabOverviewVisible = false;
            tabOverviewClosing = true;
            tabOverviewClosingSettling = false;
            tabOverviewClosingSettleFramesRendered = 0;
            tabOverviewAnimationFrameReady = false;
            tabOverviewOpeningFramesRendered = 0;
            tabOverviewClosingFramesRendered = 0;
            tabOverviewZoomFrom = overviewZoom;
            tabOverviewZoomTo = 1.0f;
            tabOverviewTransitionDuration = tabOverviewDurationForDistance(overviewZoom, 1.0f);
            tabOverviewClosingStart = GetTickCount64();
            tabOverviewAnimating = false;
            if (selectedView) {
                SetWindowPos(selectedView, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_SHOWWINDOW);
            }
            // Refresh this page's snapshot after the overlay has finished closing.
            // This replaces stale skeleton previews after a tab was selected mid-load.
            scheduleTabThumbnailCapture(tab, 420);
            startChromeAnimationTimer();
            positionOverlayWindows(true);
        }
        // activateTab raises the selected WKView last; restore overlay z-order after
        // that so an open sidebar remains above it without touching the renderer.
        positionOverlayWindows(true);
        if (tab->thumbnailCaptureNeeded || tab->previewURL != tab->activeUrl)
            scheduleTabThumbnailCapture(tab, 420);
        invalidateTabChrome(tab);
        if (previouslyActive < tabs.size() && previouslyActive != index)
            invalidateTabChrome(tabs[previouslyActive].get());
    }

    void closeTab(size_t index)
    {
        if (index >= tabs.size())
            return;

        const bool keepOverviewOpen = tabOverviewVisible;

        if (tabs.size() == 1) {
            // Closing the final tab should leave Aurora alive on a fresh start page.
            tabOverviewVisible = false;
            if (addressEditing)
                endAddressEditing();
            loadStartPage(*tabs.front());
            activateTab(0);
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
            tabSwitchAnimating = false;
            if (auto* tab = active()) {
                const std::wstring text = tab->activeUrl == L"about:blank" ? L"" : tab->activeUrl;
                SetWindowTextW(addressBar, text.c_str());
            }
            resize();
            InvalidateRect(window, nullptr, FALSE);
            if (tabOverviewWindow && IsWindowVisible(tabOverviewWindow))
                InvalidateRect(tabOverviewWindow, nullptr, FALSE);
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
        tab.initialPageLoadPending = false;
        std::string html = sourceHtml;
        // HTML strings are passed to WebKit as NUL-terminated UTF-8; remove any
        // embedded NUL introduced by old persisted browser data before conversion.
        html.erase(std::remove(html.begin(), html.end(), '\0'), html.end());
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
body{margin:0;background:#1c1c1e;color:#f5f5f7;font-family:-apple-system,BlinkMacSystemFont,"Inter","Segoe UI Variable Text","Segoe UI",Arial,sans-serif}
.wrap{max-width:980px;margin:auto;padding:54px 32px}
.item{padding:16px 18px;background:#2c2c2e;border:1px solid #48484a;border-radius:12px;margin:9px 0;display:block;color:inherit;text-decoration:none}
.name{font-size:15px}.url{font-size:12px;color:#a1a1a6;margin-top:5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.empty{padding:22px;color:#a1a1a6}
)CSS" : R"CSS(
:root{color-scheme:light}
body{margin:0;background:#f5f6f7;color:#202428;font-family:-apple-system,BlinkMacSystemFont,"Inter","Segoe UI Variable Text","Segoe UI",Arial,sans-serif}
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

        const std::string bg = dark ? "#1c1c1e" : "#f5f5f7";
        const std::string panel = dark ? "#242426" : "#ffffff";
        const std::string line = dark ? "#414145" : "#dedee2";
        const std::string text = dark ? "#f5f5f7" : "#1d1d1f";
        const std::string muted = dark ? "#a1a1a6" : "#6e6e73";
        const std::string hover = dark ? "#2e2e31" : "#f7f7f9";
        const std::string blue = dark ? "#0a84ff" : "#0879f9";
        const std::string danger = dark ? "#ff6961" : "#c9342d";

        std::string html = "<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Downloads</title><style>"
            ":root{color-scheme:" + std::string(dark ? "dark" : "light") + ";--bg:" + bg
            + ";--panel:" + panel + ";--glass:" + (dark ? "rgba(42,42,45,.78)" : "rgba(255,255,255,.78)")
            + ";--line:" + line + ";--text:" + text + ";--muted:" + muted
            + ";--hover:" + hover + ";--blue:" + blue + ";--danger:" + danger + "}"
            + "*{box-sizing:border-box}"
            "html,body{margin:0;min-height:100%;background:radial-gradient(ellipse at 12% 0%,rgba(24,145,180,.12),transparent 48%),radial-gradient(ellipse at 88% 4%,rgba(128,91,220,.12),transparent 42%),var(--bg);background-attachment:fixed;color:var(--text);"
            "font-family:-apple-system,BlinkMacSystemFont,'Inter','Segoe UI Variable Text','Segoe UI',Arial,sans-serif;"
            "-webkit-font-smoothing:antialiased}"
            "@keyframes downloads-enter{from{opacity:0;transform:translateY(7px)}to{opacity:1;transform:translateY(0)}}.wrap{width:min(960px,calc(100% - 48px));margin:0 auto;padding:56px 0 72px;animation:downloads-enter .28s cubic-bezier(.2,.75,.25,1) both}"
            ".page-head{display:flex;align-items:baseline;justify-content:space-between;gap:16px;margin:0 0 24px}"
            "h1{margin:0;font-size:30px;line-height:1.2;font-weight:600;letter-spacing:-.45px}"
            ".count{font-size:12px;color:var(--muted);white-space:nowrap}"
            ".list{overflow:hidden;border:1px solid var(--line);border-radius:16px;background:var(--glass);backdrop-filter:blur(24px) saturate(1.3);-webkit-backdrop-filter:blur(24px) saturate(1.3);box-shadow:0 14px 34px rgba(0,0,0,.1),inset 0 1px 0 rgba(255,255,255,.035)}"
            ".item{position:relative;display:grid;grid-template-columns:42px minmax(0,1fr) auto;"
            "align-items:center;gap:15px;min-height:91px;padding:14px 18px;background:transparent;transition:background .2s ease,transform .2s cubic-bezier(.2,.7,.2,1)}"
            ".item+.item{border-top:1px solid var(--line)}.item:hover{background:var(--hover)}"
            ".file-icon{width:36px;height:44px;display:flex;align-items:center;justify-content:center;color:var(--muted)}"
            ".file-icon svg{width:31px;height:39px;display:block;overflow:visible}"
            ".file-icon .sheet{fill:" + std::string(dark ? "#353538" : "#f1f2f5")
            + ";stroke:" + std::string(dark ? "#77777e" : "#c8cbd1") + ";stroke-width:1.1}"
            + ".file-icon .fold{fill:none;stroke:" + std::string(dark ? "#77777e" : "#c8cbd1") + ";stroke-width:1.1;stroke-linejoin:round}"
            + ".file-icon .mark{stroke:var(--blue);stroke-width:1.5;stroke-linecap:round;fill:none}"
            ".detail{min-width:0}.name{font-size:14px;font-weight:500;line-height:1.35;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
            ".location{font-size:12px;color:var(--muted);margin-top:3px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
            ".meta{font-size:12px;color:var(--muted);margin-top:7px;line-height:1.25}"
            ".bar{height:4px;width:100%;min-width:80px;background:" + std::string(dark ? "#4a4a4e" : "#e3e5e9")
            + ";border-radius:99px;overflow:hidden;margin-top:8px}"
            + ".bar[hidden]{display:none}.fill{height:100%;width:0;background:var(--blue);border-radius:99px;transition:width .16s linear}"
            ".bar.indeterminate .fill{position:relative;left:0;width:28%!important;-webkit-animation:download-sweep 1.15s ease-in-out infinite alternate;animation:download-sweep 1.15s ease-in-out infinite alternate}"
            "@-webkit-keyframes download-sweep{from{left:0}to{left:72%}}"
            "@keyframes download-sweep{from{left:0}to{left:72%}}"
            ".actions{display:flex;align-items:center;justify-content:flex-end;gap:3px;padding-left:8px}"
            ".action{display:inline-flex;align-items:center;justify-content:center;min-height:28px;padding:5px 8px;"
            "border:0;border-radius:7px;background:transparent;color:var(--blue);font:inherit;font-size:12px;"
            "text-decoration:none;white-space:nowrap;cursor:pointer;transition:background .18s ease,transform .18s cubic-bezier(.2,.7,.2,1),box-shadow .18s ease}"
            ".action:hover{background:var(--hover);transform:translateY(-1px)}.action:active{transform:scale(.98)}.action.cancel{color:var(--danger)}"
            ".action.pause{color:var(--muted)}.action.resume{color:var(--blue)}"
            ".action:focus-visible{outline:2px solid var(--blue);outline-offset:1px}"
            ".empty{min-height:170px;display:flex;flex-direction:column;align-items:center;justify-content:center;"
            "text-align:center;padding:30px;color:var(--muted)}"
            ".empty svg{width:32px;height:34px;margin-bottom:13px;stroke:currentColor;fill:none;stroke-width:1.5;stroke-linecap:round;stroke-linejoin:round}"
            ".empty strong{font-size:14px;font-weight:500;color:var(--text)}"
            ".empty span{font-size:12px;margin-top:5px}"
            "@media(prefers-reduced-motion:reduce){*,*::before,*::after{animation-duration:.01ms!important;animation-iteration-count:1!important;transition-duration:.01ms!important;scroll-behavior:auto!important}}@media(max-width:640px){.wrap{width:calc(100% - 24px);padding-top:32px}.page-head{margin-bottom:18px}"
            ".item{grid-template-columns:32px minmax(0,1fr);gap:12px;padding:13px 12px}"
            ".file-icon{width:30px}.actions{grid-column:2;padding:0;justify-content:flex-start;margin-top:2px}"
            ".name{white-space:normal;overflow-wrap:anywhere}}"
            "</style></head><body><main class='wrap'><header class='page-head'><h1>Downloads</h1>"
            "<span class='count'>" + std::to_string(downloads.size()) + (downloads.size() == 1 ? " item" : " items")
            + "</span></header><div class='list'>";

        if (downloads.empty()) {
            html += "<div class='empty'><svg viewBox='0 0 32 36' aria-hidden='true'><path d='M6 2.5h13l7 7V33H6z'/>"
                "<path d='M19 2.5v7h7M16 13v11m-5-5 5 5 5-5M10 29h12'/></svg>"
                "<strong>No Downloads Yet</strong><span>Files you download will appear here.</span></div>";
        } else {
            for (size_t i = 0; i < downloads.size(); ++i) {
                const auto& item = downloads[i];
                const std::string id = std::to_string(i);
                const bool inProgress = !item.finished && !item.failed && !item.cancelled && !item.paused;
                const bool finished = item.finished && !item.failed && !item.cancelled;
                const long long percent = item.totalBytes > 0
                    ? std::clamp<long long>((item.bytesWritten * 100) / item.totalBytes, 0, 100) : 0;

                std::string status;
                if (item.cancelled) status = "Cancelled";
                else if (item.failed) status = "Download Failed";
                else if (finished) status = "Downloaded - " + formatDownloadBytes(item.totalBytes > 0 ? item.totalBytes : item.bytesWritten);
                else if (item.pausePending) status = "Pausing...";
                else if (item.resumePending) status = "Resuming...";
                else if (item.paused && item.resumeUnavailable) status = "Paused - this download cannot be resumed";
                else if (item.paused && item.totalBytes > 0)
                    status = "Paused - " + formatDownloadBytes(item.bytesWritten) + " of " + formatDownloadBytes(item.totalBytes);
                else if (item.paused) status = "Paused - " + formatDownloadBytes(item.bytesWritten) + " received";
                else if (item.bytesWritten > 0 && item.totalBytes > 0)
                    status = "Downloading - " + formatDownloadBytes(item.bytesWritten) + " of " + formatDownloadBytes(item.totalBytes);
                else if (item.bytesWritten > 0) status = "Downloading - " + formatDownloadBytes(item.bytesWritten) + " received";
                else status = "Starting download";

                std::wstring containingFolder = item.path;
                const size_t finalSeparator = containingFolder.find_last_of(L"\\/");
                if (finalSeparator != std::wstring::npos)
                    containingFolder.resize(finalSeparator);
                const size_t folderSeparator = containingFolder.find_last_of(L"\\/");
                if (folderSeparator != std::wstring::npos)
                    containingFolder = containingFolder.substr(folderSeparator + 1);
                if (containingFolder.empty())
                    containingFolder = L"Downloads";

                html += "<section class='item'><div class='file-icon' aria-hidden='true'><svg viewBox='0 0 32 40'>"
                    "<path class='sheet' d='M5.5 1.5h13l8 8V37.5H5.5z'/>"
                    "<path class='fold' d='M18.5 1.5v8h8'/>"
                    "<path class='mark' d='M10 20h12M10 25h12M10 30h8'/></svg></div>"
                    "<div class='detail'><div class='name' title='" + toUTF8(htmlEscape(item.filename)) + "'>"
                    + toUTF8(htmlEscape(item.filename)) + "</div><div class='location'>In "
                    + toUTF8(htmlEscape(containingFolder)) + "</div>"
                    "<div class='meta' id='download-meta-" + id + "'>" + status + "</div>"
                    "<div class='bar";
                if (inProgress && item.totalBytes <= 0)
                    html += " indeterminate";
                html += "' id='download-bar-" + id + "' style='display:";
                html += inProgress ? "block" : "none";
                html += "'><div class='fill' id='download-fill-" + id + "' style='width:"
                    + std::to_string(percent) + "%'></div></div></div><div class='actions'>"
                    "<a class='action' id='download-folder-" + id + "' href='aurora-download://folder?id=" + id
                    + "' style='display:" + std::string(finished ? "inline-flex" : "none") + "'>Show in Folder</a>"
                    "<a class='action pause' id='download-pause-" + id + "' href='aurora-download://pause?id=" + id
                    + "' style='display:" + std::string(inProgress && !item.pausePending && !item.resumePending ? "inline-flex" : "none") + "'>Pause</a>"
                    "<a class='action resume' id='download-resume-" + id + "' href='aurora-download://resume?id=" + id
                    + "' style='display:" + std::string(item.paused && !item.resumeData.empty() && !item.resumePending ? "inline-flex" : "none") + "'>Resume</a>"
                    "<a class='action cancel' id='download-cancel-" + id + "' href='aurora-download://cancel?id=" + id
                    + "' style='display:" + std::string(!finished && !item.failed && !item.cancelled ? "inline-flex" : "none") + "'>Cancel</a>"
                    "</div></section>";
            }
        }

        html += "</div></main></body></html>";
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


    void startDownloadsAnimationTimer()
    {
        if (window && downloadsPopoverWindow && downloadsHaveInProgress() && !downloadsAnimationTimerActive) {
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

    void invalidateDownloadPopupRows(bool redrawRows = false)
    {
        if (!downloadsPopoverWindow || !IsWindow(downloadsPopoverWindow))
            return;

        RECT client { };
        if (!GetClientRect(downloadsPopoverWindow, &client))
            return;

        const int visibleRows = static_cast<int>(std::min<size_t>(5, downloads.size()));
        const int rowHeight = scaleForDpi(window, 58);
        const int topOffset = scaleForDpi(window, 8) + scaleForDpi(window, 34);

        for (int row = 0; row < visibleRows; ++row) {
            const size_t index = downloads.size() - 1 - static_cast<size_t>(row);
            const auto& download = downloads[index];
            const bool inProgress = !download.finished && !download.failed && !download.cancelled && !download.paused;
            if (!redrawRows && !inProgress)
                continue;

            const int rowTop = topOffset + row * rowHeight;
            RECT rowRect {
                scaleForDpi(window, 6), rowTop,
                std::max(scaleForDpi(window, 7), static_cast<int>(client.right) - scaleForDpi(window, 6)),
                rowTop + rowHeight
            };
            InvalidateRect(downloadsPopoverWindow, &rowRect, FALSE);
        }
    }
    void animateDownloadsPopup()
    {
        if (!downloadsPopoverWindow || !IsWindow(downloadsPopoverWindow)) {
            stopDownloadsAnimationTimer();
            return;
        }

        bool animationPending = false;
        const size_t first = downloads.size() > 5 ? downloads.size() - 5 : 0;
        for (size_t i = first; i < downloads.size(); ++i) {
            auto& item = downloads[i];
            const bool inProgress = !item.finished && !item.failed && !item.cancelled && !item.paused;
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

    void refreshDownloadsPresentation(bool redrawPopupRows = false)
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
                const bool inProgress = !item.finished && !item.failed && !item.cancelled && !item.paused;
                const std::string status = item.cancelled ? "Cancelled"
                    : item.failed ? "Download failed"
                    : item.finished ? "Downloaded - " + formatDownloadBytes(item.totalBytes > 0 ? item.totalBytes : item.bytesWritten)
                    : item.pausePending ? "Pausing..."
                    : item.resumePending ? "Resuming..."
                    : (item.paused && item.resumeUnavailable ? "Paused - this download cannot be resumed"
                        : item.paused && item.totalBytes > 0
                            ? "Paused - " + formatDownloadBytes(item.bytesWritten) + " of " + formatDownloadBytes(item.totalBytes)
                            : item.paused
                                ? "Paused - " + formatDownloadBytes(item.bytesWritten) + " received"
                                : (item.bytesWritten > 0 && item.totalBytes > 0
                                    ? "Downloading - " + formatDownloadBytes(item.bytesWritten) + " of " + formatDownloadBytes(item.totalBytes)
                                    : (item.bytesWritten > 0 ? "Downloading - " + formatDownloadBytes(item.bytesWritten) + " received"
                                        : "Starting download")));
                script += "var s=document.getElementById('download-meta-" + id + "');if(s)s.textContent='" + status + "';";
                script += "var f=document.getElementById('download-fill-" + id + "');if(f)f.style.width='" + std::to_string(percent) + "%';";
                script += "var b=document.getElementById('download-bar-" + id + "');if(b){b.style.display='" + std::string(inProgress ? "block" : "none") + "';b.classList.toggle('indeterminate'," + std::string(inProgress && item.totalBytes <= 0 ? "true" : "false") + ");}";
                const bool canPause = inProgress && !item.pausePending && !item.resumePending;
                const bool canResume = item.paused && !item.resumeData.empty() && !item.resumePending && !item.cancelled && !item.failed;
                const bool canCancel = !item.finished && !item.failed && !item.cancelled;
                script += "var p=document.getElementById('download-pause-" + id + "');if(p)p.style.display='" + std::string(canPause ? "inline-flex" : "none") + "';";
                script += "var r=document.getElementById('download-resume-" + id + "');if(r)r.style.display='" + std::string(canResume ? "inline-flex" : "none") + "';";
                script += "var c=document.getElementById('download-cancel-" + id + "');if(c)c.style.display='" + std::string(canCancel ? "inline-flex" : "none") + "';";
                script += "var d=document.getElementById('download-folder-" + id + "');if(d)d.style.display='" + std::string(item.finished && !item.failed && !item.cancelled ? "inline-flex" : "none") + "';";
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

        if (downloadsPopoverWindow && IsWindow(downloadsPopoverWindow)) {
            if (downloadsHaveInProgress())
                startDownloadsAnimationTimer();
            else
                stopDownloadsAnimationTimer();
            // Transfer callbacks can arrive faster than display refresh. Let the
            // animation timer own progress/status paints to prevent two paint sources racing.
            if (redrawPopupRows)
                invalidateDownloadPopupRows(true);
        }
    }

    void showDownloadsMenu()
    {
        showDownloadsPopover(this);
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
body{margin:0;background:#1c1c1e;color:#f5f5f7;font-family:-apple-system,BlinkMacSystemFont,"Inter","Segoe UI Variable Text","Segoe UI",Arial,sans-serif}
.item{padding:15px 18px;background:#2c2c2e;border:1px solid #48484a;border-radius:12px;margin:8px 0}
.name{font-size:15px}.meta{font-size:12px;color:#a1a1a6;margin-top:5px}
.item a{color:inherit;text-decoration:none}.empty{padding:22px;color:#a1a1a6}
)CSS" : R"CSS(
:root{color-scheme:light}
body{margin:0;background:#f5f6f7;color:#202428;font-family:-apple-system,BlinkMacSystemFont,"Inter","Segoe UI Variable Text","Segoe UI",Arial,sans-serif}
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
        InvalidateRect(window, nullptr, FALSE);
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
        InvalidateRect(window, nullptr, FALSE);
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
        showSharePopover(this);
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

        if (tabOverviewClosing) {
            const float currentZoom = tabOverviewZoomProgress();
            tabOverviewZoomFrom = currentZoom;
            tabOverviewZoomTo = 0.0f;
            tabOverviewTransitionDuration = tabOverviewDurationForDistance(currentZoom, 0.0f);
            tabOverviewClosing = false;
            tabOverviewClosingSettling = false;
            tabOverviewClosingSettleFramesRendered = 0;
            tabOverviewClosingTab = -1;
            tabOverviewVisible = true;
            tabOverviewAnimating = true;
            tabOverviewAnimationStart = GetTickCount64();
            tabOverviewOpeningFramesRendered = 0;
            tabOverviewAnimationFrameReady = false;
            hoveredOverviewTab = -1;
            hoveredOverviewNewTab = false;
            startChromeAnimationTimer();
            positionOverlayWindows(true);
            if (tabOverviewWindow)
                InvalidateRect(tabOverviewWindow, nullptr, FALSE);
            return;
        }

        if (tabOverviewVisible) {
            activateTab(activeTab);
            return;
        }

        if (sidebarAnimating) {
            // Settle the drawer at its target state; the overlay layout below will
            // place it once, without a redundant hide/show in the middle of overview entry.
            sidebarAnimating = false;
            sidebarOverlayAnimationFrameReady = false;
        }

        if (addressEditing)
            endAddressEditing();

        // Do not capture the live WebKit surface synchronously on the UI thread
        // while opening the overview. GPU-backed BitBlt plus thumbnail scaling can
        // stall the same thread that must deliver animation frames. Use the cached
        // preview here; a fresh capture is already scheduled after tab selection.
        tabOverviewClosing = false;
        tabOverviewClosingSettling = false;
        tabOverviewClosingSettleFramesRendered = 0;
        tabOverviewClosingTab = -1;
        tabOverviewAnimationFrameReady = false;
        tabOverviewOpeningFramesRendered = 0;
        tabOverviewClosingFramesRendered = 0;
        tabOverviewZoomFrom = 1.0f;
        tabOverviewZoomTo = 0.0f;
        tabOverviewTransitionDuration = 420;
        tabOverviewVisible = true;
        tabOverviewAnimating = true;
        tabOverviewAnimationStart = GetTickCount64();
        hoveredOverviewTab = -1;
        hoveredOverviewNewTab = false;
        startChromeAnimationTimer();
        // Lay out only the overlay. resize() would invalidate and relayout the
        // accelerated page underneath, which is unnecessary for an overlay transition.
        positionOverlayWindows(true);
        SetFocus(window);
        if (tabOverviewWindow)
            InvalidateRect(tabOverviewWindow, nullptr, FALSE);
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
        InvalidateRect(window, nullptr, FALSE);
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

    void requestActivePageRepaint(bool refreshBackingScale = false, bool signalTransitionReady = false,
        bool preserveOverlayStack = false)
    {
        if (!window || IsIconic(window) || tabOverviewVisible)
            return;
        auto* selected = active();
        if (!selected)
            return;
        WKPageRef page = WKViewGetPage(selected->view.get());
        if (!page)
            return;
        if (refreshBackingScale) {
            const double targetScale = windowScaleFactor(window);
            if (std::abs(WKPageGetBackingScaleFactor(page) - targetScale) > 0.001)
                WKPageSetCustomBackingScaleFactor(page, targetScale);
        }

        HWND viewWindow = WKViewGetWindow(selected->view.get());
        if (viewWindow && IsWindow(viewWindow)) {
            if (!IsWindowVisible(viewWindow))
                ShowWindow(viewWindow, SW_SHOWNA);
            if (!preserveOverlayStack) {
                SetWindowPos(viewWindow, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                        | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_SHOWWINDOW);
            }
            // preserveOverlayStack only suppresses the WebKit HWND z-order change.
            // The view still needs a queued, non-erasing paint request after the
            // sidebar transition; otherwise the accelerated page can remain black.
            InvalidateRect(viewWindow, nullptr, FALSE);
            RedrawWindow(viewWindow, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
        }

        // RedrawWindow alone can invalidate the wrapper HWND without making WebKit's
        // accelerated page generate a fresh frame. WKPageForceRepaint requests that frame.
        WKPageForceRepaint(page, signalTransitionReady ? static_cast<void*>(window) : nullptr,
            forcedPageRepaintFinished);
        if (!preserveOverlayStack)
            positionOverlayWindows(!sidebarAnimating && !tabOverviewAnimating && !tabOverviewClosing);
    }

    void refreshActivePageAfterSidebarTransition()
    {
        if (!window || IsIconic(window) || tabOverviewVisible || tabOverviewClosing)
            return;
        auto* selected = active();
        if (!selected)
            return;
        WKPageRef page = WKViewGetPage(selected->view.get());
        if (!page)
            return;

        // Request a new accelerated WebKit frame without a synthetic DOM resize,
        // which can cause site-wide layout and a visible white/black flash.
        requestActivePageRepaint(false, false, true);
    }

    void redrawActiveWebView(bool immediate = false)
    {
        if (!window || IsIconic(window) || tabOverviewVisible)
            return;
        auto* selected = active();
        if (!selected)
            return;
        HWND viewWindow = WKViewGetWindow(selected->view.get());
        if (!viewWindow || !IsWindowVisible(viewWindow))
            return;

        UINT flags = RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE;
        if (immediate)
            flags |= RDW_UPDATENOW;
        RedrawWindow(viewWindow, nullptr, nullptr, flags);
    }

    void redrawAfterLayoutChange()
    {
        if (!window || IsIconic(window))
            return;
        // Never block the browser UI thread waiting for a WebKit/GPU surface.
        // Force a real WebKit render asynchronously, then queue native chrome paint.
        requestActivePageRepaint(true);
        InvalidateRect(window, nullptr, FALSE);
        RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE);
    }

    void resize(bool invalidateChrome = true)
    {
        // A minimized HWND reports a 0x0 client size. Do not propagate that size
        // into WKView child HWNDs: some WebKit GPU surfaces never recover cleanly.
        if (!window || IsIconic(window))
            return;

        RECT client { };
        if (!GetClientRect(window, &client))
            return;

        if (invalidateChrome) {
            RECT pill = addressPillRect();
            const int targetX = pill.left + scaleForDpi(window, 30);
            const int targetY = pill.top + scaleForDpi(window, 2);
            const int targetWidth = std::max<int>(scaleForDpi(window, 140),
                static_cast<int>(pill.right - pill.left - scaleForDpi(window, 62)));
            const int targetHeight = std::max<int>(scaleForDpi(window, 24),
                static_cast<int>(pill.bottom - pill.top - scaleForDpi(window, 4)));
            RECT current { };
            GetWindowRect(addressBar, &current);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&current), 2);
            if (current.left != targetX || current.top != targetY
                || current.right - current.left != targetWidth
                || current.bottom - current.top != targetHeight) {
                MoveWindow(addressBar, targetX, targetY, targetWidth, targetHeight, FALSE);
            }
        }

        const int contentTop = this->contentTop();
        // Only the selected WKView participates in foreground resizing. Resizing all
        // overlapping inactive GPU surfaces on every WM_SIZE was triggering black and
        // low-resolution frames. An inactive page is resized when activateTab selects it.
        const int viewLeft = 0;
        const int viewWidth = std::max(0, static_cast<int>(client.right));
        const int viewHeight = std::max(0, static_cast<int>(client.bottom - contentTop));
        auto* selectedTab = active();
        for (const auto& tab : tabs) {
            if (tab.get() != selectedTab)
                continue;
            HWND viewWindow = WKViewGetWindow(tab->view.get());
            if (!viewWindow)
                continue;

            RECT current { };
            bool geometryChanged = !GetWindowRect(viewWindow, &current);
            if (!geometryChanged) {
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&current), 2);
                geometryChanged = current.left != viewLeft
                    || current.top != contentTop
                    || current.right - current.left != viewWidth
                    || current.bottom - current.top != viewHeight;
            }

            if (geometryChanged) {
                MoveWindow(viewWindow, viewLeft, contentTop, viewWidth, viewHeight, FALSE);
                // Only the visible active page needs a repaint after a genuine
                // viewport change. Repainting every overlapping inactive WKView can
                // expose its surface during z-order/compositor updates.
                if (tab.get() == active() && IsWindowVisible(viewWindow))
                    InvalidateRect(viewWindow, nullptr, FALSE);
            }
        }

        // Keep the visible page above inactive overlapping WKViews after resizing.
        // Inactive surfaces are deliberately left alone until their tab is selected.
        if (selectedTab) {
            HWND selectedView = WKViewGetWindow(selectedTab->view.get());
            if (selectedView && IsWindow(selectedView))
                SetWindowPos(selectedView, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER
                        | SWP_NOREDRAW | SWP_SHOWWINDOW);
        }
        if (findBarVisible)
            positionFindBar();
        positionOverlayWindows(true);
        if (invalidateChrome)
            InvalidateRect(window, nullptr, FALSE);
    }

    HBITMAP captureWindowTransitionBitmap(const RECT& rect)
    {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (width < 1 || height < 1)
            return nullptr;

        HDC screenDC = GetDC(nullptr);
        if (!screenDC)
            return nullptr;

        HDC bitmapDC = CreateCompatibleDC(screenDC);
        BITMAPINFO bitmapInfo { };
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void* bitmapPixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(screenDC, &bitmapInfo, DIB_RGB_COLORS,
            &bitmapPixels, nullptr, 0);

        if (!bitmapDC || !bitmap || !bitmapPixels) {
            if (bitmapDC)
                DeleteDC(bitmapDC);
            if (bitmap)
                DeleteObject(bitmap);
            ReleaseDC(nullptr, screenDC);
            return nullptr;
        }

        HGDIOBJ previousBitmap = SelectObject(bitmapDC, bitmap);
        const BOOL copied = BitBlt(bitmapDC, 0, 0, width, height, screenDC,
            rect.left, rect.top, SRCCOPY | CAPTUREBLT);
        if (copied) {
            auto* pixels = static_cast<DWORD*>(bitmapPixels);
            const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            for (size_t index = 0; index < pixelCount; ++index)
                pixels[index] = (pixels[index] & 0x00FFFFFFu) | 0xFF000000u;
        }

        if (previousBitmap)
            SelectObject(bitmapDC, previousBitmap);
        DeleteDC(bitmapDC);
        ReleaseDC(nullptr, screenDC);
        if (!copied) {
            DeleteObject(bitmap);
            return nullptr;
        }
        return bitmap;
    }

    bool renderWindowTransitionFrame(const RECT& rect)
    {
        if (!windowTransitionOverlay || !windowTransitionBitmap
            || windowTransitionBitmapSize.cx < 1 || windowTransitionBitmapSize.cy < 1)
            return false;

        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        const int sourceWidth = windowTransitionBitmapSize.cx;
        const int sourceHeight = windowTransitionBitmapSize.cy;
        if (width < 1 || height < 1)
            return false;

        HDC screenDC = GetDC(nullptr);
        if (!screenDC)
            return false;
        HDC sourceDC = CreateCompatibleDC(screenDC);
        HDC outputDC = CreateCompatibleDC(screenDC);
        if (!sourceDC || !outputDC) {
            if (sourceDC)
                DeleteDC(sourceDC);
            if (outputDC)
                DeleteDC(outputDC);
            ReleaseDC(nullptr, screenDC);
            return false;
        }

        BITMAPINFO bitmapInfo { };
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void* outputPixels = nullptr;
        HBITMAP outputBitmap = CreateDIBSection(screenDC, &bitmapInfo, DIB_RGB_COLORS,
            &outputPixels, nullptr, 0);
        if (!outputBitmap || !outputPixels) {
            if (outputBitmap)
                DeleteObject(outputBitmap);
            DeleteDC(sourceDC);
            DeleteDC(outputDC);
            ReleaseDC(nullptr, screenDC);
            return false;
        }

        HGDIOBJ previousSource = SelectObject(sourceDC, windowTransitionBitmap);
        HGDIOBJ previousOutput = SelectObject(outputDC, outputBitmap);
        SetStretchBltMode(outputDC, HALFTONE);
        SetBrushOrgEx(outputDC, 0, 0, nullptr);
        const BOOL stretched = StretchBlt(outputDC, 0, 0, width, height,
            sourceDC, 0, 0, sourceWidth, sourceHeight, SRCCOPY);

        BOOL updated = FALSE;
        if (stretched) {
            auto* pixels = static_cast<DWORD*>(outputPixels);
            const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            for (size_t index = 0; index < pixelCount; ++index)
                pixels[index] = (pixels[index] & 0x00FFFFFFu) | 0xFF000000u;

            // Keep the window's rounded silhouette while the captured frame scales.
            const int baseRadius = std::max(1, scaleForDpi(window, 9));
            const int radiusX = std::clamp(
                static_cast<int>(std::lround(static_cast<double>(baseRadius) * width / sourceWidth)),
                1, width / 2);
            const int radiusY = std::clamp(
                static_cast<int>(std::lround(static_cast<double>(baseRadius) * height / sourceHeight)),
                1, height / 2);
            const int antialiasWidth = std::max(1, std::min(radiusX, radiusY));

            auto setCornerAlpha = [&](int x, int y, float coverage) {
                const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width)
                    + static_cast<size_t>(x);
                DWORD pixel = pixels[index];
                const unsigned int alpha = static_cast<unsigned int>(
                    std::lround(std::clamp(coverage, 0.0f, 1.0f) * 255.0f));
                const unsigned int blue = (pixel & 0xFFu) * alpha / 255u;
                const unsigned int green = ((pixel >> 8) & 0xFFu) * alpha / 255u;
                const unsigned int red = ((pixel >> 16) & 0xFFu) * alpha / 255u;
                pixels[index] = (alpha << 24) | (red << 16) | (green << 8) | blue;
            };

            for (int y = 0; y < radiusY; ++y) {
                for (int x = 0; x < radiusX; ++x) {
                    const float dx = (static_cast<float>(radiusX) - (x + 0.5f)) / radiusX;
                    const float dy = (static_cast<float>(radiusY) - (y + 0.5f)) / radiusY;
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    const float coverage = std::clamp(
                        (1.0f - distance) * antialiasWidth + 0.5f, 0.0f, 1.0f);

                    setCornerAlpha(x, y, coverage);
                    setCornerAlpha(width - 1 - x, y, coverage);
                    setCornerAlpha(x, height - 1 - y, coverage);
                    setCornerAlpha(width - 1 - x, height - 1 - y, coverage);
                }
            }

            POINT destination { rect.left, rect.top };
            SIZE destinationSize { width, height };
            POINT source { 0, 0 };
            BLENDFUNCTION blend { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            updated = UpdateLayeredWindow(windowTransitionOverlay, screenDC,
                &destination, &destinationSize, outputDC, &source, 0, &blend, ULW_ALPHA);
        }

        if (previousSource)
            SelectObject(sourceDC, previousSource);
        if (previousOutput)
            SelectObject(outputDC, previousOutput);
        DeleteObject(outputBitmap);
        DeleteDC(sourceDC);
        DeleteDC(outputDC);
        ReleaseDC(nullptr, screenDC);
        return stretched && updated;
    }

    void releaseWindowTransitionResources()
    {
        if (windowTransitionOverlay && IsWindow(windowTransitionOverlay))
            DestroyWindow(windowTransitionOverlay);
        windowTransitionOverlay = nullptr;

        if (windowTransitionBitmap)
            DeleteObject(windowTransitionBitmap);
        windowTransitionBitmap = nullptr;
        windowTransitionBitmapSize = SIZE { 0, 0 };
    }

    void cancelWindowTransition()
    {
        if (window)
            KillTimer(window, kWindowTransitionTimerId);
        windowTransitionActive = false;
        windowTransitionGeometryApplied = false;
        windowTransitionWaitingForRepaint = false;
        windowTransitionRepaintReady = false;
        windowTransitionSettleTicks = 0;
        windowTransitionRepaintWaitStartTick = 0;
        windowTransitionLastRepaintRequestTick = 0;
        releaseWindowTransitionResources();
    }

    void setWindowBoundsImmediately(const RECT& targetRect, bool maximized)
    {
        if (!window)
            return;
        const int width = targetRect.right - targetRect.left;
        const int height = targetRect.bottom - targetRect.top;
        if (width < 1 || height < 1)
            return;

        if (SetWindowPos(window, HWND_TOP, targetRect.left, targetRect.top, width, height,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED))
            customMaximized = maximized;
        resize();
        disableNativeWindowBorder(window);
        redrawAfterLayoutChange();
    }

    void finishWindowTransition()
    {
        if (!windowTransitionActive || !window)
            return;

        KillTimer(window, kWindowTransitionTimerId);
        if (!windowTransitionGeometryApplied) {
            const RECT targetRect = windowTransitionTargetRect;
            const bool targetMaximized = windowTransitionTargetMaximized;
            const int width = targetRect.right - targetRect.left;
            const int height = targetRect.bottom - targetRect.top;
            RECT currentRect { };
            const bool geometryMatches = GetWindowRect(window, &currentRect)
                && currentRect.left == targetRect.left
                && currentRect.top == targetRect.top
                && currentRect.right == targetRect.right
                && currentRect.bottom == targetRect.bottom;
            if (width > 0 && height > 0 && !geometryMatches) {
                if (!SetWindowPos(window, HWND_TOP, targetRect.left, targetRect.top, width, height,
                        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED))
                    customMaximized = !targetMaximized;
                else
                    customMaximized = targetMaximized;
            } else if (geometryMatches) {
                customMaximized = targetMaximized;
            }

            if (!targetMaximized) {
                lastNormalWindowRect = targetRect;
                hasLastNormalWindowRect = true;
            }
            resize();
            disableNativeWindowBorder(window);
            InvalidateRect(window, nullptr, FALSE);

            // Keep the captured transition frame over the live page until WebKit
            // confirms that a fresh render has completed. Use a bounded timer as a
            // fallback so the window cannot remain covered if the callback is delayed.
            windowTransitionGeometryApplied = true;
            windowTransitionWaitingForRepaint = true;
            windowTransitionRepaintReady = false;
            windowTransitionSettleTicks = 0;
            windowTransitionRepaintWaitStartTick = GetTickCount64();
            windowTransitionLastRepaintRequestTick = windowTransitionRepaintWaitStartTick;
            requestActivePageRepaint(true, true);
            if (SetTimer(window, kWindowTransitionTimerId, 48, nullptr))
                return;
            windowTransitionWaitingForRepaint = false;
        }

        // Do not uncover the live WebKit child after one short timeout. Keep the
        // captured frame visible while requesting fresh frames and letting the GPU
        // compositor settle after restore/maximize and minimize/restore transitions.
        if (windowTransitionGeometryApplied && windowTransitionWaitingForRepaint) {
            const ULONGLONG now = GetTickCount64();
            if (windowTransitionRepaintReady) {
                if (windowTransitionSettleTicks < 3) {
                    ++windowTransitionSettleTicks;
                    requestActivePageRepaint(false, false);
                    if (windowTransitionSettleTicks < 3) {
                        SetTimer(window, kWindowTransitionTimerId, 48, nullptr);
                        return;
                    }
                }
            } else {
                if (now - windowTransitionLastRepaintRequestTick >= 160) {
                    windowTransitionLastRepaintRequestTick = now;
                    requestActivePageRepaint(false, true);
                }
                if (now - windowTransitionRepaintWaitStartTick < 2400) {
                    SetTimer(window, kWindowTransitionTimerId, 48, nullptr);
                    return;
                }
            }
        }

        windowTransitionWaitingForRepaint = false;
        windowTransitionRepaintReady = false;
        windowTransitionSettleTicks = 0;
        windowTransitionRepaintWaitStartTick = 0;
        windowTransitionLastRepaintRequestTick = 0;
        windowTransitionActive = false;
        windowTransitionGeometryApplied = false;
        releaseWindowTransitionResources();
        InvalidateRect(window, nullptr, FALSE);
        // A fallback repaint covers unusual delayed callbacks; when WebKit finishes
        // promptly the callback path reaches here with the new backing frame ready.
        beginWindowSurfaceRecovery(true);
    }

    void animateWindowTransition()
    {
        if (!windowTransitionActive)
            return;

        const ULONGLONG elapsed = GetTickCount64() - windowTransitionStartTick;
        const float progress = std::clamp(
            static_cast<float>(elapsed) / static_cast<float>(kWindowTransitionDurationMs),
            0.0f, 1.0f);
        // Ease out cubic for the quick, controlled zoom used by Safari's window controls.
        const float inverse = 1.0f - progress;
        const float eased = 1.0f - inverse * inverse * inverse;

        const int startWidth = windowTransitionStartRect.right - windowTransitionStartRect.left;
        const int startHeight = windowTransitionStartRect.bottom - windowTransitionStartRect.top;
        const int targetWidth = windowTransitionTargetRect.right - windowTransitionTargetRect.left;
        const int targetHeight = windowTransitionTargetRect.bottom - windowTransitionTargetRect.top;
        auto interpolate = [eased](int from, int to) {
            return static_cast<int>(std::lround(from + (to - from) * eased));
        };

        const int left = interpolate(windowTransitionStartRect.left, windowTransitionTargetRect.left);
        const int top = interpolate(windowTransitionStartRect.top, windowTransitionTargetRect.top);
        const int width = std::max(1, interpolate(startWidth, targetWidth));
        const int height = std::max(1, interpolate(startHeight, targetHeight));
        const RECT frame { left, top, left + width, top + height };

        if (!renderWindowTransitionFrame(frame) || progress >= 1.0f)
            finishWindowTransition();
    }

    void toggleMaximizeWindow()
    {
        if (!window || webFullscreen || windowTransitionActive)
            return;

        RECT startRect { };
        if (!GetWindowRect(window, &startRect))
            return;

        RECT targetRect { };
        const bool targetMaximized = !customMaximized;
        if (targetMaximized) {
            restoredWindowRect = startRect;
            HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info { };
            info.cbSize = sizeof(info);
            if (!monitor || !GetMonitorInfoW(monitor, &info))
                return;
            targetRect = info.rcWork;
        } else {
            targetRect = restoredWindowRect;
            if (targetRect.right <= targetRect.left || targetRect.bottom <= targetRect.top)
                return;
        }

        const int startWidth = startRect.right - startRect.left;
        const int startHeight = startRect.bottom - startRect.top;
        const int targetWidth = targetRect.right - targetRect.left;
        const int targetHeight = targetRect.bottom - targetRect.top;
        if (startWidth < 1 || startHeight < 1 || targetWidth < 1 || targetHeight < 1)
            return;
        if (startRect.left == targetRect.left && startRect.top == targetRect.top
            && startWidth == targetWidth && startHeight == targetHeight) {
            customMaximized = targetMaximized;
            return;
        }

        HBITMAP capturedFrame = captureWindowTransitionBitmap(startRect);
        if (!capturedFrame) {
            setWindowBoundsImmediately(targetRect, targetMaximized);
            return;
        }

        windowTransitionBitmap = capturedFrame;
        windowTransitionBitmapSize = SIZE { startWidth, startHeight };
        windowTransitionOverlay = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"STATIC",
            L"",
            WS_POPUP,
            startRect.left,
            startRect.top,
            startWidth,
            startHeight,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );
        if (!windowTransitionOverlay || !renderWindowTransitionFrame(startRect)) {
            releaseWindowTransitionResources();
            setWindowBoundsImmediately(targetRect, targetMaximized);
            return;
        }

        windowTransitionStartRect = startRect;
        windowTransitionTargetRect = targetRect;
        windowTransitionTargetMaximized = targetMaximized;
        windowTransitionStartTick = GetTickCount64();
        windowTransitionGeometryApplied = false;
        windowTransitionActive = true;

        // Keep the real top-level window visible throughout the transition so
        // the taskbar and foreground activation do not blink. On restore, move
        // the live window beneath the full-size captured frame before it shrinks;
        // on maximize, leave it in place until the expanding frame reaches it.
        ShowWindow(windowTransitionOverlay, SW_SHOWNOACTIVATE);
        if (!targetMaximized) {
            if (!SetWindowPos(window, HWND_TOP, targetRect.left, targetRect.top,
                    targetWidth, targetHeight,
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
                cancelWindowTransition();
                setWindowBoundsImmediately(targetRect, targetMaximized);
                return;
            }
            resize();
            disableNativeWindowBorder(window);
        }
        if (!SetTimer(window, kWindowTransitionTimerId, 16, nullptr))
            finishWindowTransition();
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
            toggleSidebar();
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
            openBrowserSettings(this, 7);
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
            openBrowserSettings(this, 12);
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

        InvalidateRect(window, nullptr, FALSE);
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
        InvalidateRect(window, nullptr, FALSE);
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
        InvalidateRect(window, nullptr, FALSE);
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

    void drawSidebarPanel(HDC dc, bool overlaySurface = false)
    {
        if (overlaySurface) {
            if (!sidebarOpen && !sidebarAnimating)
                return;
        } else if (sidebarOpen || sidebarAnimating) {
            // The dedicated overlay HWND owns the visible sidebar surface.
            return;
        }

        const int savedDC = SaveDC(dc);
        if (!savedDC)
            return;

        const bool dark = chromeDarkTheme();
        int width = scaleForDpi(window, 300);
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
        HFONT font = CreateFontW(-MulDiv(11, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HFONT secondaryFont = CreateFontW(-MulDiv(9, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HFONT headingFont = CreateFontW(-MulDiv(13, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
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
        drawRoundedControl(dc, selector,
            dark ? RGB(43, 43, 46) : RGB(236, 238, 241),
            dark ? RGB(57, 57, 60) : RGB(224, 226, 230),
            static_cast<float>(MulDiv(8, dpi, 96)));
        const int part = (selector.right - selector.left) / 3;
        for (int i = 0; i < 3; ++i) {
            RECT r { selector.left + part * i, selector.top, selector.left + part * (i + 1), selector.bottom };
            if (i == sidebarSection) {
                RECT selectedRect { r.left + scaleForDpi(window, 2), r.top + scaleForDpi(window, 2),
                    r.right - scaleForDpi(window, 2), r.bottom - scaleForDpi(window, 2) };
                drawRoundedControl(dc, selectedRect,
                    dark ? RGB(76, 76, 80) : RGB(255, 255, 255),
                    dark ? RGB(88, 88, 92) : RGB(220, 223, 228),
                    static_cast<float>(MulDiv(6, dpi, 96)));
            }
            SetTextColor(dc, i == sidebarSection ? (dark ? RGB(248, 248, 250) : RGB(32, 34, 38)) : (dark ? RGB(166, 166, 171) : RGB(103, 108, 115)));
            const wchar_t* label = i == 0 ? L"Bookmarks" : (i == 1 ? L"Reading List" : L"History");
            DrawTextW(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        int rowTop = top + scaleForDpi(window, 88);
        int rowHeight = scaleForDpi(window, 46);
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
                    if (static_cast<int>(i) == hoveredSidebarRow) {
                        const RECT hoverRect = sidebarRowRect(static_cast<int>(i));
                        const COLORREF hoverFill = dark ? RGB(48, 48, 52) : RGB(234, 237, 241);
                        drawRoundedControl(dc, hoverRect, hoverFill, hoverFill,
                            static_cast<float>(MulDiv(7, dpi, 96)));
                    }
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 3), width - scaleForDpi(window, 16), y + scaleForDpi(window, 21) };
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
                    if (static_cast<int>(i) == hoveredSidebarRow) {
                        const RECT hoverRect = sidebarRowRect(static_cast<int>(i));
                        const COLORREF hoverFill = dark ? RGB(48, 48, 52) : RGB(234, 237, 241);
                        drawRoundedControl(dc, hoverRect, hoverFill, hoverFill,
                            static_cast<float>(MulDiv(7, dpi, 96)));
                    }
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 3), width - scaleForDpi(window, 16), y + scaleForDpi(window, 21) };
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
                    if (static_cast<int>(i) == hoveredSidebarRow) {
                        const RECT hoverRect = sidebarRowRect(static_cast<int>(i));
                        const COLORREF hoverFill = dark ? RGB(48, 48, 52) : RGB(234, 237, 241);
                        drawRoundedControl(dc, hoverRect, hoverFill, hoverFill,
                            static_cast<float>(MulDiv(7, dpi, 96)));
                    }
                    RECT tr { scaleForDpi(window, 20), y + scaleForDpi(window, 3), width - scaleForDpi(window, 16), y + scaleForDpi(window, 21) };
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
        if (hoveredSidebarSettings) {
            RECT hoverRect { settingsRect.left, settingsRect.top + scaleForDpi(window, 2),
                settingsRect.right, settingsRect.bottom - scaleForDpi(window, 2) };
            const COLORREF hoverFill = dark ? RGB(48, 48, 52) : RGB(234, 237, 241);
            drawRoundedControl(dc, hoverRect, hoverFill, hoverFill,
                static_cast<float>(MulDiv(7, dpi, 96)));
        }

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
        RestoreDC(dc, savedDC);
    }

    bool handleSidebarHit(POINT point)
    {
        if (sidebarAnimating) {
            // The overlay window clips clicks to its visible portion; coordinates
            // arriving here are local to the sliding panel.
            return point.x >= 0 && point.x < scaleForDpi(window, 300)
                && point.y >= contentTop();
        }
        if (!sidebarOpen)
            return false;

        int width = scaleForDpi(window, 300);
        int top = contentTop();
        if (point.x < 0 || point.x >= width || point.y < top)
            return false;

        RECT closeRect { width - scaleForDpi(window, 52), top + scaleForDpi(window, 8),
            width - scaleForDpi(window, 6), top + scaleForDpi(window, 48) };
        if (PtInRect(&closeRect, point)) {
            toggleSidebar();
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
                if (sidebarOverlayWindow && IsWindowVisible(sidebarOverlayWindow))
                    InvalidateRect(sidebarOverlayWindow, nullptr, FALSE);
                return true;
            }
        }

        int rowTop = top + scaleForDpi(window, 88);
        int rowHeight = scaleForDpi(window, 46);
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

        HBRUSH toolbarBrush = CreateSolidBrush(dark ? RGB(44, 44, 48) : RGB(247, 248, 251));
        RECT toolbarRect { 0, 0, client.right, toolbarH };
        FillRect(dc, &toolbarRect, toolbarBrush);
        DeleteObject(toolbarBrush);

        // A restrained top-edge sheen gives the native toolbar a glass-like surface
        // without moving any controls or relying on platform-specific backdrop APIs.
        const int sheenHeight = std::max(1, scaleForDpi(window, 6));
        const COLORREF sheenTop = dark ? RGB(62, 62, 68) : RGB(255, 255, 255);
        const COLORREF sheenBottom = dark ? RGB(44, 44, 48) : RGB(247, 248, 251);
        for (int y = 0; y < sheenHeight; ++y) {
            const float weight = static_cast<float>(y) / std::max(1, sheenHeight - 1);
            HBRUSH sheenBrush = CreateSolidBrush(blendColor(sheenTop, sheenBottom, weight));
            RECT sheenRect { 0, y, client.right, y + 1 };
            FillRect(dc, &sheenRect, sheenBrush);
            DeleteObject(sheenBrush);
        }

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
        drawTrafficLight(dc, pad, lightY, light, RGB(255, 95, 87), hoveredTrafficLight == 0);
        drawTrafficLight(dc, pad + light + lightGap, lightY, light, RGB(255, 189, 46), hoveredTrafficLight == 1);
        drawTrafficLight(dc, pad + (light + lightGap) * 2, lightY, light, RGB(39, 201, 63), hoveredTrafficLight == 2);

        RECT sidebarRect = sidebarButtonRect();
        const bool sidebarHovered = hoveredLeftControl == 0;
        drawRoundedControl(dc, sidebarRect,
            sidebarHovered ? (dark ? RGB(79, 79, 84) : RGB(234, 237, 241))
                : (dark ? RGB(59, 59, 63) : RGB(251, 251, 252)),
            sidebarHovered ? (dark ? RGB(103, 103, 109) : RGB(205, 210, 216))
                : (dark ? RGB(82, 82, 86) : RGB(211, 214, 218)),
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

        const int navigationMiddle = navigationRect.left + (navigationRect.right - navigationRect.left) / 2;
        if (hoveredNavigationButton >= 0) {
            RECT hoverRect = hoveredNavigationButton == 0
                ? RECT { navigationRect.left + scaleForDpi(window, 2), navigationRect.top + scaleForDpi(window, 2),
                    navigationMiddle - scaleForDpi(window, 1), navigationRect.bottom - scaleForDpi(window, 2) }
                : RECT { navigationMiddle + scaleForDpi(window, 1), navigationRect.top + scaleForDpi(window, 2),
                    navigationRect.right - scaleForDpi(window, 2), navigationRect.bottom - scaleForDpi(window, 2) };
            const COLORREF hoverFill = dark ? RGB(77, 77, 82) : RGB(233, 236, 240);
            const COLORREF hoverBorder = dark ? RGB(88, 88, 93) : RGB(222, 225, 229);
            drawRoundedControl(dc, hoverRect, hoverFill, hoverBorder,
                static_cast<float>(navigationRect.bottom - navigationRect.top) / 2.0f);
        }

        HPEN navigationDivider = CreatePen(PS_SOLID, 1, dark ? RGB(83, 83, 87) : RGB(219, 221, 225));
        HGDIOBJ oldNavigationDivider = SelectObject(dc, navigationDivider);
        MoveToEx(dc, navigationMiddle, navigationRect.top + scaleForDpi(window, 8), nullptr);
        LineTo(dc, navigationMiddle, navigationRect.bottom - scaleForDpi(window, 8));
        SelectObject(dc, oldNavigationDivider);
        DeleteObject(navigationDivider);

        RECT backRect { navigationRect.left + gap, navigationRect.top,
            navigationMiddle, navigationRect.bottom };
        RECT forwardRect { navigationMiddle, navigationRect.top,
            navigationRect.right - gap, navigationRect.bottom };
        drawChevron(dc, backRect, false, active() && WKPageCanGoBack(WKViewGetPage(active()->view.get())), dark);
        drawChevron(dc, forwardRect, true, active() && WKPageCanGoForward(WKViewGetPage(active()->view.get())), dark);

        if (tabsH > 0) {
            HBRUSH tabsBrush = CreateSolidBrush(dark ? RGB(35, 35, 37) : RGB(242, 242, 244));
            RECT tabsRow { 0, tabsTop, client.right, tabsTop + tabsH };
            FillRect(dc, &tabsRow, tabsBrush);
            DeleteObject(tabsBrush);

            for (size_t i = 0; i < tabs.size(); ++i) {
                const RECT tabRect = tabBarItemRect(i);
                const float transitionProgress = tabSwitchEase();
                const float activeWeight = i == activeTab ? transitionProgress
                    : (tabSwitchAnimating && i == previousActiveTab ? 1.0f - transitionProgress : 0.0f);
                const COLORREF inactiveFill = dark ? RGB(47, 47, 49) : RGB(233, 234, 236);
                const COLORREF activeFill = dark ? RGB(66, 66, 69) : RGB(255, 255, 255);
                const COLORREF inactiveBorder = dark ? RGB(55, 55, 58) : RGB(229, 231, 232);
                const COLORREF activeBorder = dark ? RGB(91, 91, 95) : RGB(211, 214, 216);
                const COLORREF tabFill = blendColor(inactiveFill, activeFill, activeWeight);
                const COLORREF tabBorder = blendColor(inactiveBorder, activeBorder, activeWeight);
                const int tabRadius = static_cast<int>((tabRect.bottom - tabRect.top) / 2.0f);
                drawRoundedControl(dc, tabRect, tabFill, tabBorder,
                    static_cast<float>(tabRadius));
                if (i == activeTab && activeWeight > 0.04f) {
                    // Square the lower corners so the active tab meets the page surface.
                    RECT tabJoin { tabRect.left, tabRect.bottom - tabRadius,
                        tabRect.right, tabRect.bottom };
                    HBRUSH joinBrush = CreateSolidBrush(tabFill);
                    FillRect(dc, &tabJoin, joinBrush);
                    DeleteObject(joinBrush);
                    HPEN joinPen = CreatePen(PS_SOLID, 1, tabBorder);
                    HGDIOBJ oldJoinPen = SelectObject(dc, joinPen);
                    MoveToEx(dc, tabRect.left, tabRect.top + tabRadius, nullptr);
                    LineTo(dc, tabRect.left, tabRect.bottom);
                    MoveToEx(dc, tabRect.right - 1, tabRect.top + tabRadius, nullptr);
                    LineTo(dc, tabRect.right - 1, tabRect.bottom);
                    SelectObject(dc, oldJoinPen);
                    DeleteObject(joinPen);
                }
                if (activeWeight > 0.04f) {
                    const int inset = MulDiv(10, dpi, 96);
                    RECT tabSheen { tabRect.left + inset, tabRect.top + MulDiv(2, dpi, 96),
                        tabRect.right - inset, tabRect.top + MulDiv(3, dpi, 96) };
                    const COLORREF sheen = blendColor(dark ? RGB(47, 47, 49) : RGB(233, 234, 236),
                        dark ? RGB(101, 101, 106) : RGB(255, 255, 255), activeWeight);
                    HBRUSH tabSheenBrush = CreateSolidBrush(sheen);
                    FillRect(dc, &tabSheen, tabSheenBrush);
                    DeleteObject(tabSheenBrush);
                }
                const int iconSize = MulDiv(16, dpi, 96);
                const int iconTop = tabRect.top + (tabRect.bottom - tabRect.top - iconSize) / 2;
                RECT iconRect { tabRect.left + MulDiv(9, dpi, 96), iconTop,
                    tabRect.left + MulDiv(9, dpi, 96) + iconSize, iconTop + iconSize };
                if (showWebsiteIconsInTabs) {
                    if (tabs[i]->favicon)
                        DrawIconEx(dc, iconRect.left, iconRect.top, tabs[i]->favicon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                    else if (gAuroraAppIcon)
                        DrawIconEx(dc, iconRect.left, iconRect.top, gAuroraAppIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                    else
                        drawCompass(dc, iconRect);
                }
                RECT titleText = tabRect;
                titleText.left = showWebsiteIconsInTabs
                    ? iconRect.right + MulDiv(6, dpi, 96)
                    : tabRect.left + MulDiv(10, dpi, 96);
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
            dark ? RGB(61, 61, 66) : RGB(249, 250, 252),
            dark ? RGB(91, 91, 97) : RGB(220, 224, 230),
            static_cast<float>(pill.bottom - pill.top) / 2.0f);
        const int addressSheenInset = std::max(5, MulDiv(13, dpi, 96));
        RECT addressSheen { pill.left + addressSheenInset, pill.top + MulDiv(2, dpi, 96),
            pill.right - addressSheenInset, pill.top + MulDiv(3, dpi, 96) };
        HBRUSH addressSheenBrush = CreateSolidBrush(dark ? RGB(91, 91, 97) : RGB(255, 255, 255));
        FillRect(dc, &addressSheen, addressSheenBrush);
        DeleteObject(addressSheenBrush);
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
        if (hoveredLeftControl == 2) {
            RECT hoverRect { pill.left + MulDiv(4, dpi, 96), pill.top + MulDiv(3, dpi, 96),
                pill.left + MulDiv(28, dpi, 96), pill.bottom - MulDiv(3, dpi, 96) };
            drawRoundedControl(dc, hoverRect,
                dark ? RGB(82, 82, 87) : RGB(230, 233, 237),
                dark ? RGB(82, 82, 87) : RGB(230, 233, 237),
                static_cast<float>(hoverRect.bottom - hoverRect.top) / 2.0f);
        }
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
            if (button.right <= button.left || button.bottom <= button.top || hoveredToolbarButton != i)
                continue;
            RECT hoverRect { button.left + MulDiv(2, dpi, 96), button.top + MulDiv(2, dpi, 96),
                button.right - MulDiv(2, dpi, 96), button.bottom - MulDiv(2, dpi, 96) };
            drawRoundedControl(dc, hoverRect, hoverFill, hoverBorder,
                static_cast<float>(MulDiv(7, dpi, 96)));
        }
        drawShare(dc, shareRect, dark);
        if (downloadsButtonVisible())
            drawDownload(dc, downloadRect, dark);
        drawPlus(dc, newTabRect, dark);
        drawTabOverview(dc, overviewRect, dark);
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
    browser->persistWindowStateForThisWindow = false;
    if (source) {
        browser->themeMode = source->themeMode;
        browser->homePage = source->homePage;
        browser->searchEngine = source->searchEngine;
        browser->downloadLocation = source->downloadLocation;
        browser->rememberWindowState = source->rememberWindowState;
        browser->showStartPageFavorites = source->showStartPageFavorites;
        browser->showStartPageFrequentlyVisited = source->showStartPageFrequentlyVisited;
        browser->showStartPageReadingList = source->showStartPageReadingList;
        browser->openLinksInBackground = source->openLinksInBackground;
        browser->searchSuggestionsEnabled = source->searchSuggestionsEnabled;
        browser->showFullWebsiteAddress = source->showFullWebsiteAddress;
        browser->newTabsOpenWithHomepage = source->newTabsOpenWithHomepage;
        browser->newWindowsOpenWithHomepage = source->newWindowsOpenWithHomepage;
        browser->showWebsiteIconsInTabs = source->showWebsiteIconsInTabs;
        browser->alwaysShowTabBar = source->alwaysShowTabBar;
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
    HWND rememberWindowStateCheck { nullptr };
    HWND javascriptCheck { nullptr };
    HWND imagesCheck { nullptr };
    HWND popupsCheck { nullptr };
    HWND localStorageCheck { nullptr };
    HWND developerToolsCheck { nullptr };
    HWND startPageBackgroundLabel { nullptr };
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
    InvalidateRect(window, nullptr, FALSE);
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
        dialog->rememberWindowStateCheck = check(0,
            L"Remember window size and state between launches",
            kSettingsRememberWindowState, 32, 374, 620, browser->rememberWindowState);
        label(0, L"Restore Aurora to its previous normal or maximized state when it opens.",
            32, 408, 790, 24);

        // Appearance
        label(1, L"Browser appearance", 32, 174, 790, 22);
        dialog->themeCombo = combo(1, kSettingsThemeCombo, 32, 202, 440);
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic (follow Windows)"));
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Light"));
        SendMessageW(dialog->themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dark"));
        SendMessageW(dialog->themeCombo, CB_SETCURSEL,
            browser->themeMode == ThemeMode::System ? 0 : (browser->themeMode == ThemeMode::Light ? 1 : 2), 0);
        label(1, L"Choose the appearance for browser chrome and built-in pages.", 32, 242, 790, 28);
        actionButton(1, L"Choose Start Page Image…", kSettingsBrowseBackground, 32, 282, 210);
        actionButton(1, L"Use Default Background", kSettingsClearBackground, 254, 282, 190);
        const std::wstring backgroundDescription = browser->startPageBackground.empty()
            ? L"Using the built-in Aurora wallpaper" : browser->startPageBackground;
        dialog->startPageBackgroundLabel = label(1, backgroundDescription.c_str(), 32, 323, 790, 42);

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
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HFONT subtitleFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
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
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
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
        if (LOWORD(wParam) == kSettingsBrowseBackground) {
            if (dialog->browser->chooseStartPageBackground(window) && dialog->startPageBackgroundLabel)
                SetWindowTextW(dialog->startPageBackgroundLabel, dialog->browser->startPageBackground.c_str());
            return 0;
        }
        if (LOWORD(wParam) == kSettingsClearBackground) {
            dialog->browser->clearStartPageBackground();
            if (dialog->startPageBackgroundLabel)
                SetWindowTextW(dialog->startPageBackgroundLabel, L"Using the built-in Aurora wallpaper");
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
            browser->rememberWindowState = SendMessageW(
                dialog->rememberWindowStateCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
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

    if (browser->sidebarOpen || browser->sidebarAnimating) {
        browser->hoveredSidebarRow = -1;
        browser->hoveredSidebarSettings = false;
        browser->sidebarOpen = false;
        browser->sidebarAnimating = false;
        browser->sidebarAnimationFrom = 0.0f;
        browser->sidebarAnimationTarget = 0.0f;
        browser->sidebarOverlayAnimationFrameReady = false;
        browser->positionOverlayWindows(true);
        const RECT sidebarButton = browser->sidebarButtonRect();
        InvalidateRect(browser->window, &sidebarButton, FALSE);
    }

    for (size_t i = 0; i < browser->tabs.size(); ++i) {
        if (browser->tabs[i]->internalPage == L"settings") {
            if (i != browser->activeTab)
                browser->activateTab(i);
            if (auto* tab = browser->tabs[i].get()) {
                if (requestedSection >= 0) {
                    const int targetSection = std::clamp(requestedSection, 0, 12);
                    if (tab->settingsSection != targetSection)
                        browser->loadSettingsPage(*tab, targetSection);
                }
            }
            return;
        }
    }

    // A brand-new Settings tab has no card in the current overview. Close the
    // overview as a single state transition instead of animating from a nonexistent
    // card or leaving the overlay above the new WebKit view.
    if (browser->tabOverviewVisible) {
        browser->tabOverviewVisible = false;
        browser->tabOverviewAnimating = false;
        browser->tabOverviewClosing = false;
        browser->tabOverviewClosingTab = -1;
        browser->tabOverviewAnimationFrameReady = false;
        browser->hoveredOverviewTab = -1;
        browser->hoveredOverviewNewTab = false;
        if (browser->tabOverviewWindow && IsWindow(browser->tabOverviewWindow))
            ShowWindow(browser->tabOverviewWindow, SW_HIDE);
        browser->positionOverlayWindows(true);
    }

    if (browser->addTab(true, false)) {
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

    const std::vector<std::wstring> candidates = faviconCandidatesForPage(pageURL);
    if (candidates.empty())
        return;

    HWND window = browser->window;
    std::thread([window, pageURL, candidates] {
        HICON icon = nullptr;
        for (const auto& candidate : candidates) {
            if (!IsWindow(window))
                return;

            wchar_t cacheFile[MAX_PATH] { };
            const HRESULT hr = URLDownloadToCacheFileW(nullptr, candidate.c_str(),
                cacheFile, MAX_PATH, 0, nullptr);
            if (FAILED(hr) || !cacheFile[0])
                continue;

            icon = loadFaviconImageFile(cacheFile);
            if (icon)
                break;
        }

        if (!IsWindow(window)) {
            if (icon)
                DestroyIcon(icon);
            return;
        }

        // Return failures too, so the UI can clear the in-flight marker and allow
        // a later completed-load callback to retry instead of permanently giving up.
        auto* result = new FaviconResult { pageURL, icon };
        if (!PostMessageW(window, WM_AURORA_FAVICON_READY, 0, reinterpret_cast<LPARAM>(result))) {
            if (icon)
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
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
        HFONT urlFont = CreateFontW(-MulDiv(10, dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
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

                const auto& entry = browser->addressSuggestionEntries[i];
                const int iconSize = MulDiv(22, dpi, 96);
                const int iconLeft = row.left + MulDiv(12, dpi, 96);
                const int iconTop = row.top + (rowHeight - iconSize) / 2;
                const COLORREF iconColor = entry.searchAction
                    ? (dark ? RGB(133, 180, 245) : RGB(31, 111, 218)) : secondary;
                HPEN iconPen = CreatePen(PS_SOLID, 1, iconColor);
                HPEN priorIconPen = static_cast<HPEN>(SelectObject(dc, iconPen));
                HBRUSH priorIconBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
                if (entry.searchAction) {
                    const int lens = iconSize * 3 / 5;
                    Ellipse(dc, iconLeft + 1, iconTop + 1, iconLeft + lens, iconTop + lens);
                    MoveToEx(dc, iconLeft + lens - 2, iconTop + lens - 2, nullptr);
                    LineTo(dc, iconLeft + iconSize - 1, iconTop + iconSize - 1);
                } else {
                    HICON siteIcon = nullptr;
                    for (const auto& openTab : browser->tabs) {
                        if (openTab->favicon && openTab->activeUrl == entry.url) {
                            siteIcon = openTab->favicon;
                            break;
                        }
                    }
                    if (siteIcon) {
                        SelectObject(dc, priorIconBrush);
                        SelectObject(dc, priorIconPen);
                        DeleteObject(iconPen);
                        DrawIconEx(dc, iconLeft, iconTop, siteIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                        iconPen = nullptr;
                    } else {
                        Ellipse(dc, iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize);
                        MoveToEx(dc, iconLeft + iconSize / 2, iconTop + 1, nullptr);
                        LineTo(dc, iconLeft + iconSize / 2, iconTop + iconSize - 1);
                        Arc(dc, iconLeft + iconSize / 4, iconTop + 1, iconLeft + iconSize * 3 / 4,
                            iconTop + iconSize - 1, iconLeft + iconSize / 2, iconTop + 1,
                            iconLeft + iconSize / 2, iconTop + iconSize - 1);
                    }
                }
                if (iconPen) {
                    SelectObject(dc, priorIconBrush);
                    SelectObject(dc, priorIconPen);
                    DeleteObject(iconPen);
                }
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
                const std::wstring detail = entry.searchAction && browser
                    ? L"Search with " + browser->searchEngine : entry.url;
                DrawTextW(dc, detail.c_str(), -1, &urlRect,
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

    if (browser && browser->addressBarOriginalProcedure)
        return CallWindowProcW(browser->addressBarOriginalProcedure, window, message, wParam, lParam);

    return DefWindowProcW(window, message, wParam, lParam);
}



void discardDownloadResumeData(WKDataRef, const void*)
{
}

struct DownloadOperationContext {
    BrowserState* browser { nullptr };
    std::wstring path;
};

void copyDownloadResumeData(DownloadEntry& entry, WKDataRef data)
{
    if (!data)
        return;
    const size_t size = WKDataGetSize(data);
    const unsigned char* bytes = WKDataGetBytes(data);
    if (size && bytes)
        entry.resumeData.assign(bytes, bytes + size);
}

void pauseDownloadCompleted(WKDataRef data, const void* rawContext)
{
    std::unique_ptr<DownloadOperationContext> context(
        const_cast<DownloadOperationContext*>(static_cast<const DownloadOperationContext*>(rawContext)));
    if (!context || !context->browser)
        return;

    BrowserState* browser = context->browser;
    auto found = std::find_if(browser->downloads.rbegin(), browser->downloads.rend(),
        [&](const DownloadEntry& item) { return item.path == context->path; });
    if (found == browser->downloads.rend())
        return;

    auto& entry = *found;
    if (entry.cancelled) {
        if (!entry.path.empty())
            DeleteFileW(entry.path.c_str());
        entry.pausePending = false;
        entry.paused = false;
        entry.resumeData.clear();
        entry.resumeUnavailable = false;
        browser->refreshDownloadsPresentation(true);
        return;
    }
    if (entry.finished) {
        entry.pausePending = false;
        browser->refreshDownloadsPresentation(true);
        return;
    }

    copyDownloadResumeData(entry, data);
    WIN32_FILE_ATTRIBUTE_DATA fileInfo { };
    if (GetFileAttributesExW(entry.path.c_str(), GetFileExInfoStandard, &fileInfo)) {
        const ULONGLONG actualSize = (static_cast<ULONGLONG>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
        entry.bytesWritten = static_cast<long long>(actualSize);
    }
    entry.pausePending = false;
    entry.paused = true;
    entry.resumeUnavailable = entry.resumeData.empty();
    entry.failed = false;
    browser->refreshDownloadsPresentation(true);
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
    if (url == L"about:aurora") {
        WKFramePolicyListenerIgnore(listener);
        openBrowserSettings(browser, 12);
        return;
    }
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
        } else if (command == L"aurora-download://pause") {
            if (!entry.finished && !entry.failed && !entry.cancelled
                && !entry.paused && !entry.pausePending && !entry.resumePending && entry.download) {
                entry.pausePending = true;
                auto* context = new DownloadOperationContext { browser, entry.path };
                WKDownloadCancel(entry.download, context, pauseDownloadCompleted);
                browser->refreshDownloadsPresentation(true);
            }
        } else if (command == L"aurora-download://resume") {
            if (entry.paused && !entry.resumeData.empty() && !entry.cancelled && !entry.resumePending) {
                auto* activeTab = browser->active();
                WKPageRef page = activeTab ? WKViewGetPage(activeTab->view.get()) : nullptr;
                if (!page) {
                    entry.resumeUnavailable = true;
                    browser->refreshDownloadsPresentation(true);
                    return;
                }
                auto data = adoptWK(WKDataCreate(entry.resumeData.data(), entry.resumeData.size()));
                const std::string utf8Path = toUTF8(entry.path);
                auto path = adoptWK(WKStringCreateWithUTF8CString(utf8Path.c_str()));
                if (!data || !path) {
                    entry.resumeUnavailable = true;
                    browser->refreshDownloadsPresentation(true);
                    return;
                }
                auto* context = new DownloadOperationContext { browser, entry.path };
                entry.resumePending = true;
                entry.paused = false;
                entry.resumeUnavailable = false;
                WKPageResumeDownloadFromResumeData(page, data.get(), path.get(), context, resumeDownloadDidStart);
                browser->refreshDownloadsPresentation(true);
            }
        } else if (command == L"aurora-download://cancel") {
            if (entry.pausePending) {
                // Pause already cancels the underlying WebKit transfer asynchronously.
                // Mark the row cancelled and let its callback remove the partial file.
                entry.cancelled = true;
                entry.resumeData.clear();
                browser->refreshDownloadsPresentation(true);
            } else if (entry.paused) {
                entry.cancelled = true;
                entry.paused = false;
                entry.pausePending = false;
                entry.resumePending = false;
                entry.resumeData.clear();
                entry.resumeUnavailable = false;
                DeleteFileW(entry.path.c_str());
                browser->refreshDownloadsPresentation(true);
            } else if (entry.resumePending) {
                entry.cancelled = true;
                entry.resumeData.clear();
                browser->refreshDownloadsPresentation(true);
            } else if (!entry.finished && !entry.failed && !entry.cancelled && entry.download) {
                entry.cancelled = true;
                entry.resumeData.clear();
                WKDownloadCancel(entry.download, nullptr, discardDownloadResumeData);
                browser->refreshDownloadsPresentation(true);
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
    if (command == L"aurora-settings://browse-background") {
        browser->chooseStartPageBackground(browser->window);
        browser->loadSettingsPage(*tab, 2);
        return;
    }
    if (command == L"aurora-settings://clear-background") {
        browser->clearStartPageBackground();
        browser->loadSettingsPage(*tab, 2);
        return;
    }
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
    std::wstring backgroundTabs, showFavorites, showFrequentlyVisited, showReadingList;
    std::wstring searchSuggestions, fullWebsiteAddress;
    std::wstring newTabsOpenWith, newWindowsOpenWith, websiteIcons, alwaysShowTabBar;
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
        else if (key == L"backgroundTabs") backgroundTabs = value;
        else if (key == L"showFavorites") showFavorites = value;
        else if (key == L"showFrequentlyVisited") showFrequentlyVisited = value;
        else if (key == L"showReadingList") showReadingList = value;
        else if (key == L"searchSuggestions") searchSuggestions = value;
        else if (key == L"fullWebsiteAddress") fullWebsiteAddress = value;
        else if (key == L"newTabsOpenWith") newTabsOpenWith = value;
        else if (key == L"newWindowsOpenWith") newWindowsOpenWith = value;
        else if (key == L"websiteIcons") websiteIcons = value;
        else if (key == L"alwaysShowTabBar") alwaysShowTabBar = value;
        else if (key == L"section") section = value;
        if (amp == std::wstring::npos) break;
        position = amp + 1;
    }

    if (command == L"aurora-settings://navigate" && !section.empty()) {
        int requested = 0;
        bool validSection = true;
        for (wchar_t ch : section) {
            if (ch < L'0' || ch > L'9') {
                validSection = false;
                break;
            }
            requested = requested * 10 + static_cast<int>(ch - L'0');
            if (requested > 12) {
                validSection = false;
                break;
            }
        }
        if (validSection && requested <= 12) {
            tab->settingsSection = requested;
            return;
        }
    }

    if (theme == L"light") browser->themeMode = ThemeMode::Light;
    else if (theme == L"dark") browser->themeMode = ThemeMode::Dark;
    else browser->themeMode = ThemeMode::System;
    if (engine == L"Bing" || engine == L"DuckDuckGo" || engine == L"Google"
        || engine == L"Yahoo" || engine == L"Ecosia")
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
    browser->openLinksInBackground = backgroundTabs == L"1";
    browser->showStartPageFavorites = showFavorites == L"1";
    browser->showStartPageFrequentlyVisited = showFrequentlyVisited == L"1";
    browser->showStartPageReadingList = showReadingList == L"1";
    browser->searchSuggestionsEnabled = searchSuggestions == L"1";
    browser->showFullWebsiteAddress = fullWebsiteAddress == L"1";
    browser->newTabsOpenWithHomepage = newTabsOpenWith == L"homepage";
    browser->newWindowsOpenWithHomepage = newWindowsOpenWith == L"homepage";
    browser->showWebsiteIconsInTabs = websiteIcons == L"1";
    browser->alwaysShowTabBar = alwaysShowTabBar == L"1";
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
        // The download progress callback may not repeat the expected length
        // on every update. Capture it from the initial HTTP response first.
        const long long responseLength = response
            ? WKURLResponseGetExpectedContentLength(response) : 0;
        browser->downloads.push_back({ filenameCandidate, path, 0,
            responseLength > 0 ? responseLength : 0, false, false, download });
        if (browser->downloads.size() > 64)
            browser->downloads.erase(browser->downloads.begin());
        browser->refreshDownloadsPresentation(true);
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
            if (it->paused || it->resumePending) {
                browser->refreshDownloadsPresentation();
                return;
            }
            const long long reportedTotal = std::max(0LL, totalBytesWritten);
            const long long reportedDelta = std::max(0LL, bytesWritten);
            if (reportedTotal > it->bytesWritten)
                it->bytesWritten = reportedTotal;
            else if (reportedTotal == 0 && reportedDelta > 0)
                it->bytesWritten += reportedDelta;
            // Some WebKit ports report an unknown length (-1/0) on later callbacks.
            // Preserve a valid response length instead of reverting to indeterminate.
            if (totalBytesExpectedToWrite > 0)
                it->totalBytes = totalBytesExpectedToWrite;
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
            it->paused = false;
            it->pausePending = false;
            it->resumePending = false;
            it->resumeUnavailable = false;
            it->resumeData.clear();
            break;
        }
    }

    browser->refreshDownloadsPresentation(true);
}


void downloadDidFailWithError(WKDownloadRef download, WKErrorRef error, WKDataRef resumeData, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return;
    std::wstring failedPath;
    bool cancelled = false;
    bool pauseRelated = false;
    for (auto it = browser->downloads.rbegin(); it != browser->downloads.rend(); ++it) {
        if (it->download == download) {
            cancelled = it->cancelled;
            failedPath = it->path;
            if (cancelled) {
                it->resumeData.clear();
            } else if (it->pausePending || it->paused || it->resumePending) {
                copyDownloadResumeData(*it, resumeData);
                it->pausePending = false;
                it->paused = true;
                it->resumeUnavailable = it->resumeData.empty();
                pauseRelated = true;
            } else {
                it->failed = true;
            }
            break;
        }
    }
    if (cancelled) {
        if (!failedPath.empty())
            DeleteFileW(failedPath.c_str());
        browser->refreshDownloadsPresentation(true);
        return;
    }
    if (pauseRelated) {
        browser->refreshDownloadsPresentation(true);
        return;
    }
    browser->refreshDownloadsPresentation(true);
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

void resumeDownloadDidStart(WKDownloadRef resumedDownload, const void* rawContext)
{
    std::unique_ptr<DownloadOperationContext> context(
        const_cast<DownloadOperationContext*>(static_cast<const DownloadOperationContext*>(rawContext)));
    if (!context || !context->browser)
        return;

    BrowserState* browser = context->browser;
    auto found = std::find_if(browser->downloads.rbegin(), browser->downloads.rend(),
        [&](const DownloadEntry& item) { return item.path == context->path; });
    if (found == browser->downloads.rend()) {
        if (resumedDownload)
            WKDownloadCancel(resumedDownload, nullptr, discardDownloadResumeData);
        return;
    }

    auto& entry = *found;
    if (entry.cancelled) {
        DeleteFileW(entry.path.c_str());
        if (resumedDownload) {
            entry.download = resumedDownload;
            installDownloadClient(resumedDownload, browser);
            WKDownloadCancel(resumedDownload, nullptr, discardDownloadResumeData);
        }
        browser->refreshDownloadsPresentation(true);
        return;
    }

    if (!resumedDownload) {
        entry.resumePending = false;
        entry.paused = true;
        entry.resumeUnavailable = false;
        browser->refreshDownloadsPresentation(true);
        return;
    }

    entry.download = resumedDownload;
    entry.resumePending = false;
    entry.paused = false;
    entry.pausePending = false;
    entry.resumeUnavailable = false;
    entry.finished = false;
    entry.failed = false;
    entry.resumeData.clear();
    installDownloadClient(resumedDownload, browser);
    browser->refreshDownloadsPresentation(true);
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


struct DownloadsPopoverWindowState {
    BrowserState* browser { nullptr };
    RECT targetRect { };
    ULONGLONG openedAt { 0 };
    int hoveredRow { -1 };
    float rowHoverOpacity[7] { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    bool trackingMouse { false };
    bool animating { true };
    bool waitingForInitialButtonRelease { false };
    bool acrylicBackdrop { false };
};

int downloadsPopoverVisibleRows(const DownloadsPopoverWindowState* state)
{
    return state && state->browser
        ? static_cast<int>(std::min<size_t>(5, state->browser->downloads.size())) : 0;
}

int downloadsPopoverArrowHeight(const DownloadsPopoverWindowState* state)
{
    return scaleForDpi(state && state->browser ? state->browser->window : nullptr, 8);
}

int downloadsPopoverHeaderHeight(const DownloadsPopoverWindowState* state)
{
    return scaleForDpi(state && state->browser ? state->browser->window : nullptr, 34);
}

int downloadsPopoverRowHeight(const DownloadsPopoverWindowState* state)
{
    return scaleForDpi(state && state->browser ? state->browser->window : nullptr, 58);
}

int downloadsPopoverFooterHeight(const DownloadsPopoverWindowState* state)
{
    return scaleForDpi(state && state->browser ? state->browser->window : nullptr, 32);
}

int downloadsPopoverEmptyBodyHeight(const DownloadsPopoverWindowState* state)
{
    return scaleForDpi(state && state->browser ? state->browser->window : nullptr, 64);
}

int downloadsPopoverHeight(const DownloadsPopoverWindowState* state)
{
    const int rows = downloadsPopoverVisibleRows(state);
    const int bodyHeight = rows > 0
        ? rows * downloadsPopoverRowHeight(state) : downloadsPopoverEmptyBodyHeight(state);
    return downloadsPopoverArrowHeight(state) + downloadsPopoverHeaderHeight(state)
        + bodyHeight + downloadsPopoverFooterHeight(state)
        + scaleForDpi(state && state->browser ? state->browser->window : nullptr, 4);
}

RECT downloadsPopoverRowRect(const DownloadsPopoverWindowState* state, int index)
{
    if (!state || index < 0 || index >= downloadsPopoverVisibleRows(state))
        return { 0, 0, 0, 0 };
    const int dpi = state->browser ? windowDpi(state->browser->window) : 96;
    const int width = state->targetRect.right - state->targetRect.left;
    const int top = downloadsPopoverArrowHeight(state) + downloadsPopoverHeaderHeight(state)
        + index * downloadsPopoverRowHeight(state);
    const int horizontalPad = MulDiv(8, dpi, 96);
    return { horizontalPad, top, width - horizontalPad, top + downloadsPopoverRowHeight(state) };
}

RECT downloadsPopoverClearRect(const DownloadsPopoverWindowState* state)
{
    if (!state)
        return { 0, 0, 0, 0 };
    const int dpi = state->browser ? windowDpi(state->browser->window) : 96;
    const int width = state->targetRect.right - state->targetRect.left;
    const int top = downloadsPopoverArrowHeight(state);
    return { width - MulDiv(61, dpi, 96), top + MulDiv(4, dpi, 96),
        width - MulDiv(10, dpi, 96), top + downloadsPopoverHeaderHeight(state) - MulDiv(4, dpi, 96) };
}

RECT downloadsPopoverFooterRect(const DownloadsPopoverWindowState* state)
{
    if (!state)
        return { 0, 0, 0, 0 };
    const int width = state->targetRect.right - state->targetRect.left;
    const int rows = downloadsPopoverVisibleRows(state);
    const int bodyHeight = rows > 0
        ? rows * downloadsPopoverRowHeight(state) : downloadsPopoverEmptyBodyHeight(state);
    const int top = downloadsPopoverArrowHeight(state) + downloadsPopoverHeaderHeight(state) + bodyHeight;
    return { 0, top, width, top + downloadsPopoverFooterHeight(state) };
}

RECT downloadsPopoverActionRect(const DownloadsPopoverWindowState* state, int index)
{
    const RECT row = downloadsPopoverRowRect(state, index);
    if (row.right <= row.left)
        return { 0, 0, 0, 0 };
    const int dpi = state->browser ? windowDpi(state->browser->window) : 96;
    const int size = MulDiv(25, dpi, 96);
    const int right = row.right - MulDiv(9, dpi, 96);
    const int centerY = (row.top + row.bottom) / 2;
    return { right - size, centerY - size / 2, right, centerY + size / 2 };
}

bool downloadsPopoverHasClearableItems(const DownloadsPopoverWindowState* state)
{
    if (!state || !state->browser)
        return false;
    return std::any_of(state->browser->downloads.begin(), state->browser->downloads.end(),
        [](const DownloadEntry& item) { return item.finished || item.failed || item.cancelled; });
}

void drawDownloadsPopoverSurface(HDC dc, const RECT& rect, COLORREF fillColor,
    COLORREF borderColor, float radius, BYTE fillAlpha)
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
    Gdiplus::SolidBrush fill(Gdiplus::Color(fillAlpha,
        GetRValue(fillColor), GetGValue(fillColor), GetBValue(fillColor)));
    Gdiplus::Pen border(iconGdiColor(borderColor), 1.0f);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&border, &path);
}

bool enableDownloadsAcrylicBackdrop(HWND window, bool dark)
{
    struct AccentPolicy { int state; int flags; DWORD gradientColor; int animationId; };
    struct WindowCompositionAttributeData { int attribute; void* data; SIZE_T size; };
    using SetWindowCompositionAttributeProc = BOOL (WINAPI*)(HWND, WindowCompositionAttributeData*);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto setComposition = user32
        ? reinterpret_cast<SetWindowCompositionAttributeProc>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"))
        : nullptr;
    if (!setComposition)
        return false;

    const BYTE alpha = 0xD8;
    const BYTE red = dark ? 38 : 247;
    const BYTE green = dark ? 38 : 247;
    const BYTE blue = dark ? 42 : 250;
    AccentPolicy policy {
        4, 2,
        (static_cast<DWORD>(alpha) << 24)
            | (static_cast<DWORD>(red) << 16)
            | (static_cast<DWORD>(green) << 8)
            | static_cast<DWORD>(blue),
        0
    };
    WindowCompositionAttributeData data { 19, &policy, sizeof(policy) };
    return setComposition(window, &data) != FALSE;
}

void applyDownloadsPopoverWindowRegion(HWND window, const DownloadsPopoverWindowState* state)
{
    if (!window || !state)
        return;
    const int dpi = state->browser ? windowDpi(state->browser->window) : 96;
    const int width = state->targetRect.right - state->targetRect.left;
    const int height = state->targetRect.bottom - state->targetRect.top;
    const int arrowHeight = downloadsPopoverArrowHeight(state);
    const int corner = MulDiv(16, dpi, 96);
    const int arrowCenter = width - MulDiv(30, dpi, 96);
    const int arrowHalfWidth = MulDiv(10, dpi, 96);

    HRGN region = CreateRoundRectRgn(0, arrowHeight, width + 1, height + 1, corner, corner);
    POINT arrowPoints[3] {
        { arrowCenter, 0 },
        { arrowCenter - arrowHalfWidth, arrowHeight + 1 },
        { arrowCenter + arrowHalfWidth, arrowHeight + 1 }
    };
    HRGN arrowRegion = CreatePolygonRgn(arrowPoints, 3, WINDING);
    if (region && arrowRegion)
        CombineRgn(region, region, arrowRegion, RGN_OR);
    if (arrowRegion)
        DeleteObject(arrowRegion);
    if (region)
        SetWindowRgn(window, region, TRUE);
}

std::wstring downloadsPopoverStatus(const DownloadEntry& download)
{
    if (download.cancelled)
        return L"Stopped";
    if (download.failed)
        return L"Download Failed";
    if (download.finished)
        return fromUTF8(formatDownloadBytes(
            download.totalBytes > 0 ? download.totalBytes : download.bytesWritten));
    if (download.totalBytes > 0)
        return fromUTF8(formatDownloadBytes(download.bytesWritten)) + L" of "
            + fromUTF8(formatDownloadBytes(download.totalBytes));
    if (download.bytesWritten > 0)
        return fromUTF8(formatDownloadBytes(download.bytesWritten)) + L" received";
    return L"Starting…";
}

void drawDownloadsFileIcon(HDC dc, const RECT& rect, const std::wstring& filename, bool dark, int dpi)
{
    const int left = rect.left, top = rect.top;
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath sheet;
        addRoundedIconRectangle(sheet, static_cast<float>(left) + 0.5f,
            static_cast<float>(top) + 0.5f, static_cast<float>(width) - 1.0f,
            static_cast<float>(height) - 1.0f, static_cast<float>(MulDiv(2, dpi, 96)));
        Gdiplus::SolidBrush paper(iconGdiColor(dark ? RGB(232, 232, 236) : RGB(255, 255, 255)));
        Gdiplus::Pen edge(iconGdiColor(dark ? RGB(160, 160, 166) : RGB(198, 201, 207)), 1.0f);
        graphics.FillPath(&paper, &sheet);
        graphics.DrawPath(&edge, &sheet);
        Gdiplus::GraphicsPath fold;
        fold.AddLine(static_cast<float>(left + width - MulDiv(8, dpi, 96)),
            static_cast<float>(top + 1),
            static_cast<float>(left + width - MulDiv(8, dpi, 96)),
            static_cast<float>(top + MulDiv(8, dpi, 96)));
        fold.AddLine(static_cast<float>(left + width - MulDiv(8, dpi, 96)),
            static_cast<float>(top + MulDiv(8, dpi, 96)),
            static_cast<float>(left + width - 1),
            static_cast<float>(top + MulDiv(8, dpi, 96)));
        Gdiplus::Pen foldPen(iconGdiColor(dark ? RGB(178, 178, 184) : RGB(205, 208, 214)), 1.0f);
        graphics.DrawPath(&foldPen, &fold);
    }

    std::wstring extension;
    const size_t dot = filename.find_last_of(L'.');
    if (dot != std::wstring::npos && dot + 1 < filename.size())
        extension = filename.substr(dot + 1);
    if (extension.size() > 4)
        extension.resize(4);
    if (!extension.empty())
        CharUpperBuffW(extension.data(), static_cast<DWORD>(extension.size()));
    else
        extension = L"FILE";

    HFONT extensionFont = CreateFontW(-std::max(7, MulDiv(7, dpi, 96)), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
    HGDIOBJ oldFont = SelectObject(dc, extensionFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, dark ? RGB(79, 95, 120) : RGB(86, 103, 127));
    RECT extRect { left - MulDiv(1, dpi, 96), top + MulDiv(19, dpi, 96),
        left + width + MulDiv(1, dpi, 96), top + height - MulDiv(3, dpi, 96) };
    DrawTextW(dc, extension.c_str(), -1, &extRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    DeleteObject(extensionFont);
}

void drawDownloadsActionIcon(HDC dc, const RECT& rect, bool dark, bool reveal, bool hovered, int dpi)
{
    if (hovered) {
        const COLORREF hoverFill = dark ? RGB(75, 75, 81) : RGB(229, 232, 237);
        drawRoundedControl(dc, rect, hoverFill, hoverFill,
            static_cast<float>((rect.bottom - rect.top) / 2), 0.5f);
    }
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const COLORREF iconColor = dark ? RGB(198, 198, 204) : RGB(103, 108, 116);
    Gdiplus::Pen pen(iconGdiColor(iconColor), std::max(1.2f, static_cast<float>(dpi) / 96.0f * 1.35f));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    if (reveal) {
        const float radius = static_cast<float>(rect.right - rect.left) * 0.20f;
        graphics.DrawEllipse(&pen, cx - radius - 1.0f, cy - radius - 1.5f, radius * 2.0f, radius * 2.0f);
        graphics.DrawLine(&pen, cx + radius * 0.55f, cy + radius * 0.55f,
            cx + radius * 1.55f, cy + radius * 1.55f);
    } else {
        const float arm = static_cast<float>(rect.right - rect.left) * 0.16f;
        graphics.DrawLine(&pen, cx - arm, cy - arm, cx + arm, cy + arm);
        graphics.DrawLine(&pen, cx + arm, cy - arm, cx - arm, cy + arm);
    }
}

void drawDownloadsPopover(HDC dc, HWND window, DownloadsPopoverWindowState* state)
{
    if (!state || !state->browser)
        return;

    BrowserState* browser = state->browser;
    RECT client { };
    GetClientRect(window, &client);
    const int dpi = windowDpi(browser->window);
    const int width = client.right;
    const int arrowHeight = downloadsPopoverArrowHeight(state);
    const int headerHeight = downloadsPopoverHeaderHeight(state);
    const int headerTop = arrowHeight;
    const int headerBottom = headerTop + headerHeight;
    const bool dark = browser->chromeDarkTheme();

    const COLORREF surface = dark ? RGB(40, 40, 44) : RGB(247, 247, 250);
    const COLORREF border = dark ? RGB(76, 76, 82) : RGB(205, 207, 213);
    const COLORREF primary = dark ? RGB(248, 248, 250) : RGB(28, 28, 30);
    const COLORREF secondary = dark ? RGB(164, 164, 170) : RGB(109, 113, 120);
    const COLORREF separator = dark ? RGB(74, 74, 80) : RGB(224, 226, 231);
    const COLORREF hover = dark ? RGB(57, 57, 62) : RGB(234, 236, 241);
    const COLORREF accent = dark ? RGB(10, 132, 255) : RGB(0, 122, 255);
    const int corner = MulDiv(16, dpi, 96);
    const BYTE surfaceAlpha = 255;

    RECT panelRect { 0, arrowHeight, width, client.bottom };
    drawDownloadsPopoverSurface(dc, panelRect, surface, border, static_cast<float>(corner), surfaceAlpha);

    const int arrowCenter = width - MulDiv(30, dpi, 96);
    const int arrowHalfWidth = MulDiv(10, dpi, 96);
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath arrow;
        arrow.AddLine(static_cast<float>(arrowCenter), 0.5f,
            static_cast<float>(arrowCenter - arrowHalfWidth), static_cast<float>(arrowHeight) + 0.5f);
        arrow.AddLine(static_cast<float>(arrowCenter - arrowHalfWidth), static_cast<float>(arrowHeight) + 0.5f,
            static_cast<float>(arrowCenter + arrowHalfWidth), static_cast<float>(arrowHeight) + 0.5f);
        arrow.AddLine(static_cast<float>(arrowCenter + arrowHalfWidth), static_cast<float>(arrowHeight) + 0.5f,
            static_cast<float>(arrowCenter), 0.5f);
        Gdiplus::SolidBrush arrowFill(Gdiplus::Color(surfaceAlpha,
            GetRValue(surface), GetGValue(surface), GetBValue(surface)));
        Gdiplus::Pen arrowBorder(iconGdiColor(border), 1.0f);
        graphics.FillPath(&arrowFill, &arrow);
        graphics.DrawPath(&arrowBorder, &arrow);
    }

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ originalFont = GetCurrentObject(dc, OBJ_FONT);
    HFONT titleFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
    HFONT nameFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
    HFONT statusFont = CreateFontW(-MulDiv(10, dpi, 96), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
    HFONT clearFont = CreateFontW(-MulDiv(11, dpi, 96), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());

    RECT headerRect { 0, headerTop, width, headerBottom };
    SelectObject(dc, titleFont);
    SetTextColor(dc, primary);
    DrawTextW(dc, L"Downloads", -1, &headerRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT clearRect = downloadsPopoverClearRect(state);
    const bool hasClearableItems = downloadsPopoverHasClearableItems(state);
    if (state->rowHoverOpacity[5] > 0.01f) {
        RECT hoverRect = clearRect;
        InflateRect(&hoverRect, MulDiv(3, dpi, 96), MulDiv(1, dpi, 96));
        const COLORREF mixed = blendColor(surface, hover, state->rowHoverOpacity[5]);
        drawRoundedControl(dc, hoverRect, mixed, mixed, static_cast<float>(MulDiv(7, dpi, 96)), 0.5f);
    }
    SelectObject(dc, clearFont);
    SetTextColor(dc, hasClearableItems ? accent : secondary);
    DrawTextW(dc, L"Clear", -1, &clearRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const RECT footerRect = downloadsPopoverFooterRect(state);
    const COLORREF footerHover = dark ? RGB(59, 59, 64) : RGB(235, 237, 241);
    if (state->rowHoverOpacity[6] > 0.01f) {
        RECT hoverRect = footerRect;
        InflateRect(&hoverRect, -MulDiv(3, dpi, 96), -MulDiv(1, dpi, 96));
        const COLORREF mixed = blendColor(surface, footerHover, state->rowHoverOpacity[6]);
        drawRoundedControl(dc, hoverRect, mixed, mixed, static_cast<float>(MulDiv(7, dpi, 96)), 0.5f);
    }

    HPEN headerPen = CreatePen(PS_SOLID, 1, separator);
    HGDIOBJ oldHeaderPen = SelectObject(dc, headerPen);
    MoveToEx(dc, MulDiv(12, dpi, 96), headerBottom, nullptr);
    LineTo(dc, width - MulDiv(12, dpi, 96), headerBottom);
    SelectObject(dc, oldHeaderPen);
    DeleteObject(headerPen);

    const int visible = downloadsPopoverVisibleRows(state);
    if (visible == 0) {
        const int emptyCenterY = (headerBottom + footerRect.top) / 2;
        SelectObject(dc, statusFont);
        SetTextColor(dc, secondary);
        RECT emptyRect { MulDiv(16, dpi, 96), emptyCenterY - MulDiv(12, dpi, 96),
            width - MulDiv(16, dpi, 96), emptyCenterY + MulDiv(12, dpi, 96) };
        DrawTextW(dc, L"No Downloads Yet", -1, &emptyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        POINT cursor { -1, -1 };
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        for (int row = 0; row < visible; ++row) {
            const size_t index = browser->downloads.size() - 1 - static_cast<size_t>(row);
            const auto& download = browser->downloads[index];
            const RECT rowRect = downloadsPopoverRowRect(state, row);
            const RECT actionRect = downloadsPopoverActionRect(state, row);
            const bool inProgress = !download.finished && !download.failed && !download.cancelled && !download.paused;
            const bool finished = download.finished && !download.failed && !download.cancelled;

            if (row > 0) {
                HPEN dividerPen = CreatePen(PS_SOLID, 1, separator);
                HGDIOBJ oldPen = SelectObject(dc, dividerPen);
                MoveToEx(dc, MulDiv(12, dpi, 96), rowRect.top, nullptr);
                LineTo(dc, width - MulDiv(12, dpi, 96), rowRect.top);
                SelectObject(dc, oldPen);
                DeleteObject(dividerPen);
            }

            if (state->rowHoverOpacity[row] > 0.01f) {
                RECT hoverRect = rowRect;
                InflateRect(&hoverRect, -MulDiv(2, dpi, 96), -MulDiv(2, dpi, 96));
                const COLORREF mixed = blendColor(surface, hover, state->rowHoverOpacity[row]);
                drawRoundedControl(dc, hoverRect, mixed, mixed, static_cast<float>(MulDiv(9, dpi, 96)), 0.5f);
            }

            const int iconLeft = MulDiv(18, dpi, 96);
            const int iconTop = rowRect.top + (downloadsPopoverRowHeight(state) - MulDiv(34, dpi, 96)) / 2;
            RECT fileIconRect { iconLeft, iconTop, iconLeft + MulDiv(26, dpi, 96), iconTop + MulDiv(34, dpi, 96) };
            drawDownloadsFileIcon(dc, fileIconRect, download.filename, dark, dpi);

            const int textLeft = iconLeft + MulDiv(35, dpi, 96);
            const int textRight = actionRect.left - MulDiv(8, dpi, 96);
            RECT nameRect { textLeft, rowRect.top + MulDiv(7, dpi, 96),
                textRight, rowRect.top + MulDiv(28, dpi, 96) };
            SelectObject(dc, nameFont);
            SetTextColor(dc, primary);
            DrawTextW(dc, download.filename.c_str(), -1, &nameRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            RECT statusRect { textLeft, rowRect.top + MulDiv(28, dpi, 96),
                textRight, rowRect.top + MulDiv(45, dpi, 96) };
            SelectObject(dc, statusFont);
            SetTextColor(dc, download.failed ? (dark ? RGB(255, 130, 125) : RGB(190, 45, 39)) : secondary);
            const std::wstring status = downloadsPopoverStatus(download);
            DrawTextW(dc, status.c_str(), -1, &statusRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            if (inProgress) {
                const int barLeft = textLeft;
                const int barRight = textRight;
                const int barTop = rowRect.top + MulDiv(51, dpi, 96);
                const int barHeight = std::max(3, MulDiv(3, dpi, 96));
                const int trackWidth = std::max(1, barRight - barLeft);
                Gdiplus::Graphics graphics(dc);
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::GraphicsPath track;
                addRoundedIconRectangle(track, static_cast<float>(barLeft), static_cast<float>(barTop),
                    static_cast<float>(trackWidth), static_cast<float>(barHeight),
                    static_cast<float>(barHeight) / 2.0f);
                Gdiplus::SolidBrush trackBrush(iconGdiColor(dark ? RGB(83, 83, 89) : RGB(224, 226, 231)));
                graphics.FillPath(&trackBrush, &track);

                if (download.totalBytes > 0) {
                    const double percent = std::clamp(download.displayedProgressPercent, 0.0, 100.0);
                    const float fillWidth = static_cast<float>(trackWidth * percent / 100.0);
                    if (fillWidth > 0.5f) {
                        Gdiplus::GraphicsPath fill;
                        addRoundedIconRectangle(fill, static_cast<float>(barLeft), static_cast<float>(barTop),
                            fillWidth, static_cast<float>(barHeight), static_cast<float>(barHeight) / 2.0f);
                        Gdiplus::SolidBrush fillBrush(iconGdiColor(accent));
                        graphics.FillPath(&fillBrush, &fill);
                    }
                } else {
                    const int segmentWidth = std::min(trackWidth, std::max(MulDiv(58, dpi, 96), trackWidth / 4));
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
                    Gdiplus::SolidBrush fillBrush(iconGdiColor(accent));
                    graphics.FillPath(&fillBrush, &segment);
                }
            }

            if (inProgress || finished)
                drawDownloadsActionIcon(dc, actionRect, dark, finished,
                    state->hoveredRow == row && PtInRect(&actionRect, cursor), dpi);
        }
    }

    HPEN footerPen = CreatePen(PS_SOLID, 1, separator);
    HGDIOBJ oldFooterPen = SelectObject(dc, footerPen);
    MoveToEx(dc, MulDiv(12, dpi, 96), footerRect.top, nullptr);
    LineTo(dc, width - MulDiv(12, dpi, 96), footerRect.top);
    SelectObject(dc, oldFooterPen);
    DeleteObject(footerPen);

    RECT footerLabel { MulDiv(18, dpi, 96), footerRect.top + MulDiv(1, dpi, 96),
        width - MulDiv(12, dpi, 96), footerRect.bottom };
    SelectObject(dc, clearFont);
    SetTextColor(dc, accent);
    DrawTextW(dc, L"Show All Downloads", -1, &footerLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, originalFont);
    DeleteObject(titleFont);
    DeleteObject(nameFont);
    DeleteObject(statusFont);
    DeleteObject(clearFont);
}

void showDownloadsPopover(BrowserState* browser)
{
    if (!browser || !browser->window)
        return;
    if (browser->downloadsPopoverWindow && IsWindow(browser->downloadsPopoverWindow)) {
        DestroyWindow(browser->downloadsPopoverWindow);
        browser->downloadsPopoverWindow = nullptr;
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW cls { };
        cls.cbSize = sizeof(cls);
        cls.style = CS_DROPSHADOW;
        cls.lpfnWndProc = downloadsPopoverProcedure;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.lpszClassName = L"AuroraDownloadsPopover";
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        classRegistered = true;
    }

    const int dpi = windowDpi(browser->window);
    const int width = scaleForDpi(browser->window, 355);
    RECT anchorRect = browser->rightToolbarButtonRect(1);
    POINT anchor { anchorRect.right + MulDiv(14, dpi, 96), anchorRect.bottom };
    ClientToScreen(browser->window, &anchor);

    auto* popupState = new DownloadsPopoverWindowState;
    popupState->browser = browser;
    popupState->openedAt = GetTickCount64();
    popupState->waitingForInitialButtonRelease = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const int height = downloadsPopoverHeight(popupState);

    HMONITOR monitor = MonitorFromWindow(browser->window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo { };
    monitorInfo.cbSize = sizeof(monitorInfo);
    RECT work { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo))
        work = monitorInfo.rcWork;

    int left = anchor.x - width;
    int top = anchor.y - downloadsPopoverArrowHeight(popupState);
    if (left < work.left) left = work.left;
    if (left + width > work.right) left = std::max(work.left, work.right - width);
    if (top + height > work.bottom) top = anchor.y - height;
    top = std::clamp(top, static_cast<int>(work.top),
        std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
    popupState->targetRect = { left, top, left + width, top + height };

    HWND popup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        L"AuroraDownloadsPopover", L"", WS_POPUP,
        left, top - MulDiv(4, dpi, 96), width, height,
        browser->window, nullptr, GetModuleHandleW(nullptr), popupState);
    if (!popup) {
        delete popupState;
        return;
    }

    applyDownloadsPopoverWindowRegion(popup, popupState);
    SetLayeredWindowAttributes(popup, 0, 0, LWA_ALPHA);
    popupState->acrylicBackdrop = !browser->chromeDarkTheme() && enableDownloadsAcrylicBackdrop(popup, false);
    browser->downloadsPopoverWindow = popup;
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    SetTimer(popup, kDownloadsPopoverTimerId, 15, nullptr);
    if (browser->downloadsHaveInProgress())
        browser->startDownloadsAnimationTimer();
    InvalidateRect(popup, nullptr, FALSE);
}

LRESULT CALLBACK downloadsPopoverProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<DownloadsPopoverWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* createInfo = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<DownloadsPopoverWindowState*>(createInfo->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (state && wParam == kDownloadsPopoverTimerId) {
            BrowserState* browser = state->browser;
            if (!browser || !browser->window || !IsWindow(browser->window)) {
                DestroyWindow(window);
                return 0;
            }

            const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (state->waitingForInitialButtonRelease) {
                if (!leftButtonDown)
                    state->waitingForInitialButtonRelease = false;
            } else if (leftButtonDown) {
                POINT cursor { };
                RECT popupRect { };
                if (GetCursorPos(&cursor) && GetWindowRect(window, &popupRect) && !PtInRect(&popupRect, cursor)) {
                    DestroyWindow(window);
                    return 0;
                }
            }

            if (state->animating) {
                const ULONGLONG elapsed = GetTickCount64() - state->openedAt;
                const float t = std::clamp(static_cast<float>(elapsed) / 150.0f, 0.0f, 1.0f);
                const float eased = t * t * (3.0f - 2.0f * t);
                const BYTE alpha = static_cast<BYTE>(std::clamp(eased * 255.0f, 0.0f, 255.0f));
                const int dpi = windowDpi(browser->window);
                const int lift = static_cast<int>(MulDiv(4, dpi, 96) * (1.0f - eased) + 0.5f);
                SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA);
                SetWindowPos(window, HWND_TOPMOST, state->targetRect.left, state->targetRect.top - lift,
                    state->targetRect.right - state->targetRect.left,
                    state->targetRect.bottom - state->targetRect.top, SWP_NOACTIVATE | SWP_NOZORDER);
                if (t >= 1.0f)
                    state->animating = false;
            }

            for (int i = 0; i < 7; ++i) {
                const float target = state->hoveredRow == i ? 1.0f : 0.0f;
                const float before = state->rowHoverOpacity[i];
                const float delta = target - before;
                if (delta > -0.015f && delta < 0.015f)
                    state->rowHoverOpacity[i] = target;
                else
                    state->rowHoverOpacity[i] += delta * 0.32f;
                if (std::abs(state->rowHoverOpacity[i] - before) > 0.001f) {
                    RECT hoverRect = i < downloadsPopoverVisibleRows(state)
                        ? downloadsPopoverRowRect(state, i)
                        : i == 5 ? downloadsPopoverClearRect(state) : downloadsPopoverFooterRect(state);
                    InvalidateRect(window, &hoverRect, FALSE);
                }
            }

            // Unknown-size downloads use a moving segment. Drive its repaint from
            // the popover timer itself so the segment visibly sweeps side to side.
            const int progressRows = downloadsPopoverVisibleRows(state);
            for (int row = 0; row < progressRows; ++row) {
                const size_t index = browser->downloads.size() - 1 - static_cast<size_t>(row);
                const auto& download = browser->downloads[index];
                const bool inProgress = !download.finished && !download.failed && !download.cancelled && !download.paused;
                if (inProgress && download.totalBytes <= 0) {
                    const RECT progressRow = downloadsPopoverRowRect(state, row);
                    InvalidateRect(window, &progressRow, FALSE);
                }
            }
            return 0;
        }
        break;

    case WM_MOUSEMOVE:
        if (state) {
            if (!state->trackingMouse) {
                TRACKMOUSEEVENT tracking { };
                tracking.cbSize = sizeof(tracking);
                tracking.dwFlags = TME_LEAVE;
                tracking.hwndTrack = window;
                state->trackingMouse = TrackMouseEvent(&tracking) != FALSE;
            }
            POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int hovered = -1;
            for (int i = 0; i < downloadsPopoverVisibleRows(state); ++i) {
                const RECT rect = downloadsPopoverRowRect(state, i);
                if (PtInRect(&rect, point)) {
                    hovered = i;
                    break;
                }
            }
            const RECT clearRect = downloadsPopoverClearRect(state);
            const RECT footerRect = downloadsPopoverFooterRect(state);
            if (PtInRect(&clearRect, point))
                hovered = 5;
            else if (PtInRect(&footerRect, point))
                hovered = 6;
            if (hovered != state->hoveredRow) {
                const int previous = state->hoveredRow;
                if (previous >= 0) {
                    const RECT previousRect = previous < downloadsPopoverVisibleRows(state)
                        ? downloadsPopoverRowRect(state, previous)
                        : previous == 5 ? downloadsPopoverClearRect(state) : downloadsPopoverFooterRect(state);
                    InvalidateRect(window, &previousRect, FALSE);
                }
                state->hoveredRow = hovered;
                if (hovered >= 0) {
                    const RECT currentRect = hovered < downloadsPopoverVisibleRows(state)
                        ? downloadsPopoverRowRect(state, hovered)
                        : hovered == 5 ? downloadsPopoverClearRect(state) : downloadsPopoverFooterRect(state);
                    InvalidateRect(window, &currentRect, FALSE);
                }
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        if (state && state->hoveredRow >= 0) {
            const int previous = state->hoveredRow;
            const RECT previousRect = previous < downloadsPopoverVisibleRows(state)
                ? downloadsPopoverRowRect(state, previous)
                : previous == 5 ? downloadsPopoverClearRect(state) : downloadsPopoverFooterRect(state);
            state->hoveredRow = -1;
            state->trackingMouse = false;
            InvalidateRect(window, &previousRect, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (state && state->browser) {
            BrowserState* browser = state->browser;
            POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const RECT clearRect = downloadsPopoverClearRect(state);
            const RECT footerRect = downloadsPopoverFooterRect(state);
            if (PtInRect(&footerRect, point)) {
                DestroyWindow(window);
                if (browser->addTab(true))
                    browser->loadDownloadsPage();
                return 0;
            }
            if (PtInRect(&clearRect, point) && downloadsPopoverHasClearableItems(state)) {
                auto& entries = browser->downloads;
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                    [](const DownloadEntry& item) { return item.finished || item.failed || item.cancelled; }),
                    entries.end());
                browser->refreshDownloadsPresentation(true);
                if (entries.empty()) {
                    DestroyWindow(window);
                    return 0;
                }

                const int newHeight = downloadsPopoverHeight(state);
                state->targetRect.bottom = state->targetRect.top + newHeight;
                SetWindowPos(window, nullptr, state->targetRect.left, state->targetRect.top,
                    state->targetRect.right - state->targetRect.left, newHeight,
                    SWP_NOACTIVATE | SWP_NOZORDER);
                applyDownloadsPopoverWindowRegion(window, state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }

            const int rows = downloadsPopoverVisibleRows(state);
            for (int row = 0; row < rows; ++row) {
                const RECT rowRect = downloadsPopoverRowRect(state, row);
                if (!PtInRect(&rowRect, point))
                    continue;
                const size_t index = browser->downloads.size() - 1 - static_cast<size_t>(row);
                if (index >= browser->downloads.size())
                    return 0;

                auto& download = browser->downloads[index];
                const RECT actionRect = downloadsPopoverActionRect(state, row);
                const bool inProgress = !download.finished && !download.failed && !download.cancelled && !download.paused;
                const bool finished = download.finished && !download.failed && !download.cancelled;
                if (PtInRect(&actionRect, point)) {
                    if (inProgress && download.download) {
                        download.cancelled = true;
                        WKDownloadCancel(download.download, nullptr, discardDownloadResumeData);
                        browser->refreshDownloadsPresentation(true);
                        InvalidateRect(window, &rowRect, FALSE);
                    } else if (finished && !download.path.empty()) {
                        browser->showFileInFolder(download.path);
                    }
                    return 0;
                }

                if (finished && !download.path.empty()) {
                    DestroyWindow(window);
                    ShellExecuteW(browser->window, L"open", download.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                return 0;
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT paint { };
        HDC dc = BeginPaint(window, &paint);
        RECT client { };
        GetClientRect(window, &client);
        HDC bufferDC = CreateCompatibleDC(dc);
        HBITMAP bufferBitmap = bufferDC
            ? CreateCompatibleBitmap(dc, std::max(1L, client.right), std::max(1L, client.bottom)) : nullptr;
        if (bufferDC && bufferBitmap) {
            HGDIOBJ oldBitmap = SelectObject(bufferDC, bufferBitmap);
            const bool dark = state && state->browser && state->browser->chromeDarkTheme();
            HBRUSH background = CreateSolidBrush(dark ? RGB(40, 40, 44) : RGB(247, 247, 250));
            FillRect(bufferDC, &client, background);
            DeleteObject(background);
            if (state)
                drawDownloadsPopover(bufferDC, window, state);
            BitBlt(dc, 0, 0, client.right, client.bottom, bufferDC, 0, 0, SRCCOPY);
            SelectObject(bufferDC, oldBitmap);
            DeleteObject(bufferBitmap);
            DeleteDC(bufferDC);
        } else {
            if (bufferDC)
                DeleteDC(bufferDC);
            if (state)
                drawDownloadsPopover(dc, window, state);
        }
        EndPaint(window, &paint);
        return 0;
    }

    case WM_NCDESTROY:
        if (state) {
            if (state->browser) {
                if (state->browser->downloadsPopoverWindow == window)
                    state->browser->downloadsPopoverWindow = nullptr;
                state->browser->stopDownloadsAnimationTimer();
            }
            delete state;
        }
        KillTimer(window, kDownloadsPopoverTimerId);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

struct SharePopoverWindowState {
    BrowserState* browser { nullptr };
    RECT targetRect { };
    ULONGLONG openedAt { 0 };
    int hoveredRow { -1 };
    float rowHoverOpacity[3] { 0.0f, 0.0f, 0.0f };
    bool trackingMouse { false };
    bool animating { true };
    bool waitingForInitialButtonRelease { false };
};

RECT sharePopoverRowRect(const SharePopoverWindowState* state, int index)
{
    if (!state || index < 0 || index > 2)
        return { 0, 0, 0, 0 };
    const int dpi = state->browser ? windowDpi(state->browser->window) : 96;
    const int padding = scaleForDpi(state->browser ? state->browser->window : nullptr, 5);
    const int rowHeight = MulDiv(27, dpi, 96);
    const int separatorGap = MulDiv(7, dpi, 96);
    const int top = padding + index * rowHeight + (index >= 2 ? separatorGap : 0);
    return { padding, top, state->targetRect.right - state->targetRect.left - padding, top + rowHeight };
}

void showSharePopover(BrowserState* browser)
{
    if (!browser || !browser->window)
        return;
    if (browser->sharePopoverWindow && IsWindow(browser->sharePopoverWindow)) {
        DestroyWindow(browser->sharePopoverWindow);
        browser->sharePopoverWindow = nullptr;
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW cls { };
        cls.cbSize = sizeof(cls);
        cls.style = CS_DROPSHADOW;
        cls.lpfnWndProc = sharePopoverProcedure;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.lpszClassName = L"AuroraSharePopover";
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        classRegistered = true;
    }

    const int dpi = windowDpi(browser->window);
    const int width = MulDiv(216, dpi, 96);
    const int rowHeight = MulDiv(27, dpi, 96);
    const int padding = MulDiv(5, dpi, 96);
    const int separatorGap = MulDiv(7, dpi, 96);
    const int height = padding * 2 + rowHeight * 3 + separatorGap;
    RECT anchor = browser->rightToolbarButtonRect(0);
    POINT screen { anchor.right, anchor.bottom + MulDiv(4, dpi, 96) };
    ClientToScreen(browser->window, &screen);
    HMONITOR monitor = MonitorFromWindow(browser->window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo { };
    monitorInfo.cbSize = sizeof(monitorInfo);
    RECT work { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo))
        work = monitorInfo.rcWork;
    int left = screen.x - width;
    int top = screen.y;
    if (left < work.left) left = work.left;
    if (left + width > work.right) left = std::max(work.left, work.right - width);
    if (top + height > work.bottom) top = screen.y - height - MulDiv(8, dpi, 96);
    top = std::clamp(top, static_cast<int>(work.top), std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));

    auto* popupState = new SharePopoverWindowState;
    popupState->browser = browser;
    popupState->targetRect = { left, top, left + width, top + height };
    popupState->openedAt = GetTickCount64();
    popupState->waitingForInitialButtonRelease = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    HWND popup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        L"AuroraSharePopover", L"",
        WS_POPUP,
        left, top - MulDiv(5, dpi, 96), width, height,
        browser->window, nullptr, GetModuleHandleW(nullptr), popupState);
    if (!popup) {
        delete popupState;
        return;
    }

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, MulDiv(12, dpi, 96), MulDiv(12, dpi, 96));
    if (region)
        SetWindowRgn(popup, region, TRUE);
    SetLayeredWindowAttributes(popup, 0, 0, LWA_ALPHA);
    browser->sharePopoverWindow = popup;
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    SetTimer(popup, kSharePopoverTimerId, 15, nullptr);
    InvalidateRect(popup, nullptr, FALSE);
}

LRESULT CALLBACK sharePopoverProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<SharePopoverWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* createInfo = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<SharePopoverWindowState*>(createInfo->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (state && wParam == kSharePopoverTimerId) {
            const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (state->waitingForInitialButtonRelease) {
                // The Share popover opens on the initiating button-down. Do not
                // mistake that same click for an outside click before it is released.
                if (!leftButtonDown)
                    state->waitingForInitialButtonRelease = false;
            } else if (leftButtonDown) {
                POINT cursor { };
                RECT popupRect { };
                if (GetCursorPos(&cursor) && GetWindowRect(window, &popupRect)
                    && !PtInRect(&popupRect, cursor)) {
                    DestroyWindow(window);
                    return 0;
                }
            }
            if (state->animating) {
                const ULONGLONG elapsed = GetTickCount64() - state->openedAt;
                const float t = std::clamp(static_cast<float>(elapsed) / 145.0f, 0.0f, 1.0f);
                const float eased = t * t * (3.0f - 2.0f * t);
                const BYTE alpha = static_cast<BYTE>(std::clamp(eased * 255.0f, 0.0f, 255.0f));
                const int dpi = state->browser ? windowDpi(state->browser->window) : 96;
                const int lift = static_cast<int>(MulDiv(5, dpi, 96) * (1.0f - eased) + 0.5f);
                SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA);
                SetWindowPos(window, HWND_TOPMOST,
                    state->targetRect.left, state->targetRect.top - lift,
                    state->targetRect.right - state->targetRect.left,
                    state->targetRect.bottom - state->targetRect.top,
                    SWP_NOACTIVATE | SWP_NOZORDER);
                if (t >= 1.0f)
                    state->animating = false;
            }

            // Ease each row's hover surface over a few frames; keep this timer only
            // while the popover is alive so outside-click dismissal still works.
            for (int i = 0; i < 3; ++i) {
                const float target = state->hoveredRow == i ? 1.0f : 0.0f;
                const float before = state->rowHoverOpacity[i];
                const float delta = target - before;
                if (delta > -0.015f && delta < 0.015f)
                    state->rowHoverOpacity[i] = target;
                else
                    state->rowHoverOpacity[i] += delta * 0.32f;
                if (std::abs(state->rowHoverOpacity[i] - before) > 0.001f) {
                    const RECT rowRect = sharePopoverRowRect(state, i);
                    InvalidateRect(window, &rowRect, FALSE);
                }
            }
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (state) {
            if (!state->trackingMouse) {
                TRACKMOUSEEVENT track { };
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window;
                state->trackingMouse = TrackMouseEvent(&track) != FALSE;
            }
            const int y = GET_Y_LPARAM(lParam);
            int row = -1;
            for (int i = 0; i < 3; ++i) {
                const RECT rect = sharePopoverRowRect(state, i);
                if (y >= rect.top && y < rect.bottom) {
                    row = i;
                    break;
                }
            }
            if (row != state->hoveredRow) {
                if (state->hoveredRow >= 0) {
                    const RECT oldRect = sharePopoverRowRect(state, state->hoveredRow);
                    InvalidateRect(window, &oldRect, FALSE);
                }
                state->hoveredRow = row;
                if (row >= 0) {
                    const RECT newRect = sharePopoverRowRect(state, row);
                    InvalidateRect(window, &newRect, FALSE);
                }
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) {
            state->trackingMouse = false;
            if (state->hoveredRow >= 0) {
                const RECT oldRect = sharePopoverRowRect(state, state->hoveredRow);
                state->hoveredRow = -1;
                InvalidateRect(window, &oldRect, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state && state->browser) {
            const int y = GET_Y_LPARAM(lParam);
            int row = -1;
            for (int i = 0; i < 3; ++i) {
                const RECT rect = sharePopoverRowRect(state, i);
                if (y >= rect.top && y < rect.bottom) {
                    row = i;
                    break;
                }
            }
            BrowserState* browser = state->browser;
            UINT command = row == 0 ? kShareCopyLink : row == 1 ? kShareEmailLink : row == 2 ? kMenuAddBookmark : 0;
            if (command)
                browser->handleCommand(command);
            DestroyWindow(window);
            return 0;
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint { };
        HDC dc = BeginPaint(window, &paint);
        RECT client { };
        GetClientRect(window, &client);
        const bool dark = state && state->browser && state->browser->chromeDarkTheme();
        const COLORREF fill = dark ? RGB(39, 39, 42) : RGB(255, 255, 255);
        const COLORREF border = dark ? RGB(70, 70, 75) : RGB(222, 225, 229);
        drawRoundedControl(dc, client, fill, border, static_cast<float>(MulDiv(11, state && state->browser ? windowDpi(state->browser->window) : 96, 96)));
        if (state) {
            const int dpi = state->browser ? windowDpi(state->browser->window) : 96;
            const int topPadding = MulDiv(5, dpi, 96);
            const int rowHeight = MulDiv(27, dpi, 96);
            const int sepGap = MulDiv(7, dpi, 96);
            const COLORREF textColor = dark ? RGB(239, 239, 243) : RGB(38, 41, 45);
            const COLORREF hoverFill = dark ? RGB(60, 60, 65) : RGB(239, 242, 246);
            HFONT font = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFontFamily());
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            const wchar_t* labels[] = { L"Copy Link", L"Email Link", L"Add Bookmark" };
            for (int i = 0; i < 3; ++i) {
                const RECT row = sharePopoverRowRect(state, i);
                if (state->rowHoverOpacity[i] > 0.02f) {
                    RECT hover = row;
                    InflateRect(&hover, -MulDiv(3, dpi, 96), -MulDiv(1, dpi, 96));
                    const COLORREF rowFill = blendColor(fill, hoverFill, state->rowHoverOpacity[i]);
                    drawRoundedControl(dc, hover, rowFill, rowFill, static_cast<float>(MulDiv(6, dpi, 96)));
                }
                RECT label { row.left + MulDiv(12, dpi, 96), row.top, row.right - MulDiv(8, dpi, 96), row.bottom };
                SetTextColor(dc, textColor);
                DrawTextW(dc, labels[i], -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            HPEN separatorPen = CreatePen(PS_SOLID, 1, dark ? RGB(62, 62, 66) : RGB(232, 234, 237));
            HGDIOBJ oldPen = SelectObject(dc, separatorPen);
            const int separatorY = topPadding + rowHeight * 2 + sepGap / 2;
            MoveToEx(dc, client.left + MulDiv(10, dpi, 96), separatorY, nullptr);
            LineTo(dc, client.right - MulDiv(10, dpi, 96), separatorY);
            SelectObject(dc, oldPen);
            DeleteObject(separatorPen);
            SelectObject(dc, oldFont);
            DeleteObject(font);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        if (state) {
            if (state->browser && state->browser->sharePopoverWindow == window)
                state->browser->sharePopoverWindow = nullptr;
            delete state;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}


WKPageRef createNewTabForWebKit(WKPageRef, WKPageConfigurationRef, WKNavigationActionRef, WKWindowFeaturesRef, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return nullptr;

    const size_t previouslyActive = browser->activeTab;
    if (!browser->addTab(true, false))
        return nullptr;

    auto* newTab = browser->active();
    if (!newTab)
        return nullptr;

    // Retain the returned page before optionally restoring the previous active tab.
    WKRetainPtr<WKPageRef> newPage = WKViewGetPage(newTab->view.get());
    if (!newPage)
        return nullptr;

    if (browser->openLinksInBackground && previouslyActive < browser->tabs.size() - 1)
        browser->activateTab(previouslyActive);

    // WebKit expects the returned WKPageRef to carry a retained reference.
    return newPage.leakRef();
}

void getContextMenuFromProposedMenu(WKPageRef, WKArrayRef proposedMenu, WKArrayRef* newMenu,
    WKHitTestResultRef hitTestResult, WKTypeRef, const void* clientInfo)
{
    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (browser)
        browser->pendingContextMenuLinkURL.clear();
    if (!newMenu)
        return;

    WKRetainPtr<WKURLRef> linkURL = adoptWK(
        hitTestResult ? WKHitTestResultCopyAbsoluteLinkURL(hitTestResult) : nullptr);
    WKRetainPtr<WKStringRef> linkString;
    WKRetainPtr<WKContextMenuItemRef> newTabItem;

    if (linkURL) {
        linkString = adoptWK(WKURLCopyString(linkURL.get()));
        if (linkString) {
            if (browser)
                browser->pendingContextMenuLinkURL = createString(linkString.get());
            WKRetainPtr<WKStringRef> title = adoptWK(WKStringCreateWithUTF8CString("Open Link in New Tab"));
            newTabItem = adoptWK(WKContextMenuItemCreateAsAction(
                kAuroraOpenLinkInNewTabTag, title.get(), true));
        }
    }

    const size_t count = proposedMenu ? WKArrayGetSize(proposedMenu) : 0;
    std::vector<WKTypeRef> items;
    items.reserve(count + (newTabItem ? 1 : 0));
    bool insertedNewTab = false;

    for (size_t i = 0; i < count; ++i) {
        WKTypeRef value = WKArrayGetItemAtIndex(proposedMenu, i);
        if (newTabItem && !insertedNewTab && value
            && WKGetTypeID(value) == WKContextMenuItemGetTypeID()) {
            auto item = reinterpret_cast<WKContextMenuItemRef>(value);
            const auto tag = WKContextMenuItemGetTag(item);
            if (tag == kWKContextMenuItemTagOpenLinkInNewWindow || tag == kWKContextMenuItemTagOpenLink) {
                items.push_back(reinterpret_cast<WKTypeRef>(newTabItem.get()));
                insertedNewTab = true;
            }
        }
        items.push_back(value);
    }

    if (newTabItem && !insertedNewTab)
        items.insert(items.begin(), reinterpret_cast<WKTypeRef>(newTabItem.get()));

    *newMenu = WKArrayCreate(items.data(), items.size());
}

void customContextMenuItemSelected(WKPageRef, WKContextMenuItemRef item, const void* clientInfo)
{
    if (!item || WKContextMenuItemGetTag(item) != kAuroraOpenLinkInNewTabTag)
        return;

    auto* browser = const_cast<BrowserState*>(static_cast<const BrowserState*>(clientInfo));
    if (!browser)
        return;

    // WebKit reconstructs menu items before this callback, so application user data
    // is not guaranteed to survive that conversion. Keep the hit-tested URL in
    // BrowserState while the context menu is open instead.
    const std::wstring targetURL = browser->pendingContextMenuLinkURL;
    browser->pendingContextMenuLinkURL.clear();
    if (targetURL.empty() || !browser->addTab(true, false))
        return;

    if (auto* tab = browser->active())
        browser->loadURL(*tab, targetURL);
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
        tab->thumbnailCaptureNeeded = true;
        const std::wstring completedURL = createString(adoptWK(WKPageCopyActiveURL(page)).get());
        if (!completedURL.empty())
            tab->lastCompletedThumbnailURL = completedURL;
        tab->browser->runExtensionScripts(tab);
        auto url = createString(adoptWK(WKPageCopyActiveURL(page)).get());
        if (!url.empty()
            && (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0)
            && url != tab->lastRecordedUrl) {
            tab->activeUrl = url;
            tab->browser->recordVisit(url, tab->title.empty() ? url : tab->title);
            tab->lastRecordedUrl = url;
        }
        if (tab == tab->browser->active()) {
            tab->browser->updateActiveStartPageSidebarOffset();
            tab->browser->scheduleTabThumbnailCapture(tab, 320);
            if (!tab->favicon && tab->faviconRequestURL.empty()
                && tab->faviconAttempts < 2 && !tab->activeUrl.empty()) {
                tab->faviconRequestURL = tab->activeUrl;
                ++tab->faviconAttempts;
                requestFavicon(tab->browser, tab->activeUrl);
            }
        }
    }

    // Page thumbnails are refreshed on an explicit tab switch or when overview
    // opens. Avoid capturing in this loading callback: synthetic screenshot work
    // here can stall/flicker the active WebKit surface at navigation completion.
    tab->browser->invalidateTabChrome(tab);
    if (tab == tab->browser->active()
        && (tab->browser->sidebarOpen || tab->browser->sidebarAnimating)) {
        tab->browser->positionOverlayWindows(true);
        tab->browser->updateSidebarZOrderTimer();
    }
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

    tab->browser->invalidateTabChrome(tab);
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
        tab->faviconAttempts = 0;
        // Never draw pixels from the previous URL under the new tab title.
        if (tab->previewBitmap) {
            DeleteObject(tab->previewBitmap);
            tab->previewBitmap = nullptr;
        }
        tab->previewWidth = 0;
        tab->previewHeight = 0;
        tab->previewURL.clear();
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
        const wchar_t* addressText = (url == L"about:blank" || url.empty()) ? L"" : url.c_str();
        // Avoid resetting the native Edit control on repeated URL notifications.
        setWindowTextIfChanged(tab->browser->addressBar, addressText);
    }

    if (tab == tab->browser->active()
        && (tab->browser->sidebarOpen || tab->browser->sidebarAnimating)) {
        tab->browser->positionOverlayWindows(true);
        tab->browser->updateSidebarZOrderTimer();
    }
    tab->browser->invalidateTabChrome(tab);

    if (url.empty() || url == L"about:blank")
        return;

    if ((url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0)
        && tab->faviconRequestURL != url && tab->faviconAttempts < 2) {
        tab->faviconRequestURL = url;
        ++tab->faviconAttempts;
        requestFavicon(tab->browser, url);
    }

    if (url != tab->lastRecordedUrl && (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0)) {
        tab->browser->recordVisit(url, tab->title);
        tab->lastRecordedUrl = url;
    }

    tab->browser->invalidateTabChrome(tab);
}

void didChangeEstimatedProgress(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (!tab || !tab->browser || tab != tab->browser->active())
        return;

    auto page = WKViewGetPage(tab->view.get());
    const bool loading = page && WKPageGetEstimatedProgress(page) < 1.0;
    BrowserState* browser = tab->browser;
    // The reload/stop glyph is binary. Repainting Smart Search for every fractional
    // progress notification caused the address pill to visibly flicker.
    if (browser->lastReloadButtonLoading == loading)
        return;
    browser->lastReloadButtonLoading = loading;

    const RECT pill = browser->addressPillRect();
    RECT reloadRect { pill.right - scaleForDpi(browser->window, 30), pill.top, pill.right, pill.bottom };
    if (reloadRect.right > reloadRect.left && reloadRect.bottom > reloadRect.top)
        InvalidateRect(browser->window, &reloadRect, FALSE);
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

    case WM_NCPAINT:
        // Windows may restore a default frame during activation/composition changes.
        // Disable DWM's outer border again and suppress native non-client painting.
        disableNativeWindowBorder(window);
        return 0;

    case WM_NCACTIVATE:
        // Keep the browser borderless when focus moves between windows.
        disableNativeWindowBorder(window);
        return TRUE;

    case WM_DWMCOMPOSITIONCHANGED:
        disableNativeWindowBorder(window);
        return 0;

    case WM_CREATE:
        if (!state || !state->initialize(window)) {
            MessageBoxW(window, L"WebKit could not initialize Aurora.", windowTitle, MB_OK | MB_ICONERROR);
            return -1;
        }
        return 0;

    case WM_TIMER:
        if (state && wParam == kDeferredTabLoadTimerId) {
            KillTimer(window, kDeferredTabLoadTimerId);
            state->loadPendingInitialTabPages();
            return 0;
        }
        if (state && wParam == kDeferredThumbnailCaptureTimerId) {
            KillTimer(window, kDeferredThumbnailCaptureTimerId);
            state->servicePendingTabThumbnailCapture();
            return 0;
        }
        if (state && wParam == kWindowTransitionTimerId) {
            state->animateWindowTransition();
            return 0;
        }
        if (state && wParam == kWindowSurfaceRecoveryTimerId) {
            if (state->windowSurfaceRecoveryActive)
                state->recoverWindowSurfaceFrame();
            else
                KillTimer(window, kWindowSurfaceRecoveryTimerId);
            return 0;
        }
        if (state && wParam == kSidebarZOrderTimerId) {
            state->maintainSidebarZOrder();
            return 0;
        }
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
            const int previousHovered = state->hoveredToolbarButton;
            const int previousLeft = state->hoveredLeftControl;
            const int previousNavigation = state->hoveredNavigationButton;
            const int previousSidebarRow = state->hoveredSidebarRow;
            const bool previousSidebarSettings = state->hoveredSidebarSettings;
            const int previousLight = state->hoveredTrafficLight;
            state->hoveredToolbarButton = -1;
            state->hoveredLeftControl = -1;
            state->hoveredNavigationButton = -1;
            state->hoveredSidebarRow = -1;
            state->hoveredSidebarSettings = false;
            state->hoveredTrafficLight = -1;
            for (float& opacity : state->toolbarHoverOpacity)
                opacity = 0.0f;
            if (previousHovered >= 0) {
                const RECT rect = state->rightToolbarButtonRect(previousHovered);
                if (rect.right > rect.left && rect.bottom > rect.top)
                    InvalidateRect(window, &rect, FALSE);
            }
            if (previousLeft >= 0) {
                const RECT rect = state->leftToolbarControlRect(previousLeft);
                if (rect.right > rect.left && rect.bottom > rect.top)
                    InvalidateRect(window, &rect, FALSE);
            }
            if (previousNavigation >= 0) {
                const RECT rect = state->navigationPillRect();
                InvalidateRect(window, &rect, FALSE);
            }
            if (previousSidebarRow >= 0) {
                const RECT rect = state->sidebarRowRect(previousSidebarRow);
                state->invalidateSidebarOverlayRect(rect);
            }
            if (previousSidebarSettings) {
                const RECT rect = state->sidebarSettingsRect();
                state->invalidateSidebarOverlayRect(rect);
            }
            if (previousLight >= 0) {
                const RECT rect = state->trafficLightHitRect(previousLight);
                InvalidateRect(window, &rect, FALSE);
            }
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint { };
        HDC dc = BeginPaint(window, &paint);
        if (state) {
            RECT client { };
            GetClientRect(window, &client);
            const int width = static_cast<int>(client.right - client.left);
            const int height = static_cast<int>(client.bottom - client.top);
            HDC bufferDC = width > 0 && height > 0 ? CreateCompatibleDC(dc) : nullptr;
            if (bufferDC) {
                if (!state->chromePaintBitmap || state->chromePaintBitmapSize.cx != width
                    || state->chromePaintBitmapSize.cy != height) {
                    if (state->chromePaintBitmap)
                        DeleteObject(state->chromePaintBitmap);
                    state->chromePaintBitmap = CreateCompatibleBitmap(dc, width, height);
                    state->chromePaintBitmapSize = state->chromePaintBitmap
                        ? SIZE { width, height } : SIZE { 0, 0 };
                }
                if (state->chromePaintBitmap) {
                    HGDIOBJ oldBitmap = SelectObject(bufferDC, state->chromePaintBitmap);
                    state->paint(bufferDC);
                    BitBlt(dc, 0, 0, width, height, bufferDC, 0, 0, SRCCOPY);
                    SelectObject(bufferDC, oldBitmap);
                    DeleteDC(bufferDC);
                } else {
                    DeleteDC(bufferDC);
                    state->paint(dc);
                }
            } else {
                state->paint(dc);
            }
        }
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

        // Toolbar actions at the right edge must remain client-hit targets.
        // Resolve these before resize-border detection so Windows cannot turn a
        // click on Tab Overview into HTTOP/HTRIGHT on narrow or high-DPI layouts.
        if (state) {
            const int toolbarTop = state->titleBarHeight();
            const int toolbarBottom = toolbarTop + state->toolbarHeight();
            if (point.y >= toolbarTop && point.y < toolbarBottom) {
                for (int index = 0; index < 4; ++index) {
                    const RECT button = state->rightToolbarButtonRect(index);
                    if (button.right > button.left && PtInRect(&button, point))
                        return HTCLIENT;
                }
            }
        }

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
        if (state && state->downloadsPopoverWindow && wParam == VK_ESCAPE) {
            DestroyWindow(state->downloadsPopoverWindow);
            return 0;
        }
        if (state && state->tabOverviewVisible && wParam == VK_ESCAPE) {
            state->activateTab(state->activeTab);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        if (!state)
            break;

        if (state->sharePopoverWindow && IsWindow(state->sharePopoverWindow))
            DestroyWindow(state->sharePopoverWindow);

        POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (state->downloadsPopoverWindow && IsWindow(state->downloadsPopoverWindow)) {
            const RECT downloadsButton = state->rightToolbarButtonRect(1);
            // Clicking the Downloads button toggles this popover; other browser clicks dismiss it immediately.
            if (!PtInRect(&downloadsButton, point))
                DestroyWindow(state->downloadsPopoverWindow);
        }

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

    case WM_AURORA_TRANSITION_REPAINT_READY:
        if (state && state->windowTransitionActive && state->windowTransitionWaitingForRepaint) {
            state->windowTransitionRepaintReady = true;
            SetTimer(window, kWindowTransitionTimerId, 48, nullptr);
        }
        return 0;

    case WM_AURORA_FAVICON_READY:
        if (state && lParam) {
            auto* result = reinterpret_cast<FaviconResult*>(lParam);
            TabState* updatedTab = nullptr;
            std::wstring retryFaviconURL;
            for (auto& tab : state->tabs) {
                if (tab->activeUrl != result->url || tab->faviconRequestURL != result->url)
                    continue;
                if (result->icon) {
                    if (tab->favicon)
                        DestroyIcon(tab->favicon);
                    tab->favicon = result->icon;
                    result->icon = nullptr;
                    updatedTab = tab.get();
                } else {
                    tab->faviconRequestURL.clear();
                    if (tab->faviconAttempts < 2) {
                        ++tab->faviconAttempts;
                        tab->faviconRequestURL = result->url;
                        retryFaviconURL = result->url;
                    }
                }
                break;
            }
            if (result->icon)
                DestroyIcon(result->icon);
            delete result;
            if (updatedTab)
                state->invalidateTabChrome(updatedTab);
            if (!retryFaviconURL.empty())
                requestFavicon(state, retryFaviconURL);
        }
        return 0;

    case WM_COMMAND:
        // Edit controls notify their parent after the text has actually changed.
        // Use EN_CHANGE as the authoritative autocomplete trigger so typing, deletion,
        // undo and pasted text all refresh the history filter consistently.
        if (state && lParam != 0
            && reinterpret_cast<HWND>(lParam) == state->addressBar
            && HIWORD(wParam) == EN_CHANGE) {
            if (state->addressEditing) {
                wchar_t text[4096] { };
                GetWindowTextW(state->addressBar, text, static_cast<int>(std::size(text)));
                state->updateAddressSuggestions(text);
            }
            return 0;
        }
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

    case WM_SYSCOMMAND:
        if (state) {
            const UINT systemCommand = static_cast<UINT>(wParam) & 0xFFF0u;
            if (systemCommand == SC_MAXIMIZE) {
                state->toggleMaximizeWindow();
                return 0;
            }
            if (systemCommand == SC_RESTORE && !IsIconic(window) && state->customMaximized) {
                state->toggleMaximizeWindow();
                return 0;
            }
        }
        break;

    case WM_ENTERSIZEMOVE:
        if (state)
            state->userSizingWindow = true;
        return 0;

    case WM_EXITSIZEMOVE:
        if (state) {
            state->userSizingWindow = false;
            state->updateLastNormalWindowRect();
            state->resize(false);
            state->beginWindowSurfaceRecovery(true);
            if (state->sidebarOpen) {
                state->updateSidebarZOrderTimer();
            }
        }
        return 0;

    case WM_MOVE:
        if (state)
            state->updateLastNormalWindowRect();
        break;

    case WM_SIZE:
        if (state) {
            const bool minimizing = wParam == SIZE_MINIMIZED;
            const bool restoredFromMinimized = state->wasMinimized && !minimizing;
            state->wasMinimized = minimizing;

            // Preserve the last valid WKView dimensions while minimized; never
            // resize live WebKit child windows to the minimized 0x0 client rect.
            if (minimizing)
                return 0;

            state->resize();
            state->updateLastNormalWindowRect();
            if (wParam == SIZE_MAXIMIZED || restoredFromMinimized) {
                disableNativeWindowBorder(window);
                if (!restoredFromMinimized)
                    state->redrawAfterLayoutChange();
            }
            if (restoredFromMinimized)
                state->beginWindowSurfaceRecovery(true);
        }
        return 0;

    case WM_DPICHANGED:
        if (state) {
            const double targetScale = windowScaleFactor(window);
            for (auto& tab : state->tabs) {
                auto page = WKViewGetPage(tab->view.get());
                if (page && std::abs(WKPageGetBackingScaleFactor(page) - targetScale) > 0.001)
                    WKPageSetCustomBackingScaleFactor(page, targetScale);
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
            KillTimer(window, kWindowSurfaceRecoveryTimerId);
            KillTimer(window, kSidebarZOrderTimerId);
            KillTimer(window, kDeferredThumbnailCaptureTimerId);
            state->windowSurfaceRecoveryActive = false;
            state->sidebarZOrderTimerActive = false;
            state->pendingThumbnailCaptureTab = nullptr;
            state->saveWindowStateOnExit();
            state->cancelWindowTransition();
            if (state->downloadsPopoverWindow && IsWindow(state->downloadsPopoverWindow)) {
                DestroyWindow(state->downloadsPopoverWindow);
                state->downloadsPopoverWindow = nullptr;
            }
            if (state->sharePopoverWindow && IsWindow(state->sharePopoverWindow)) {
                DestroyWindow(state->sharePopoverWindow);
                state->sharePopoverWindow = nullptr;
            }
            if (state->sidebarOverlayWindow && IsWindow(state->sidebarOverlayWindow)) {
                DestroyWindow(state->sidebarOverlayWindow);
                state->sidebarOverlayWindow = nullptr;
            }
            if (state->tabOverviewWindow && IsWindow(state->tabOverviewWindow)) {
                DestroyWindow(state->tabOverviewWindow);
                state->tabOverviewWindow = nullptr;
            }
            if (state->sidebarOverlayBitmap) {
                DeleteObject(state->sidebarOverlayBitmap);
                state->sidebarOverlayBitmap = nullptr;
                state->sidebarOverlayBitmapSize = SIZE { 0, 0 };
            }
            if (state->tabOverviewBitmap) {
                DeleteObject(state->tabOverviewBitmap);
                state->tabOverviewBitmap = nullptr;
                state->tabOverviewBitmapSize = SIZE { 0, 0 };
            }
            if (state->tabOverviewFrameBitmap) {
                DeleteObject(state->tabOverviewFrameBitmap);
                state->tabOverviewFrameBitmap = nullptr;
                state->tabOverviewFrameBitmapSize = SIZE { 0, 0 };
            }
            if (state->chromePaintBitmap) {
                DeleteObject(state->chromePaintBitmap);
                state->chromePaintBitmap = nullptr;
                state->chromePaintBitmapSize = SIZE { 0, 0 };
            }
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

LRESULT CALLBACK overlayWindowProcedure(HWND overlay, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        auto* createInfo = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* state = static_cast<BrowserState*>(createInfo->lpCreateParams);
        if (!state)
            return FALSE;
        SetWindowLongPtrW(overlay, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    auto* state = reinterpret_cast<BrowserState*>(GetWindowLongPtrW(overlay, GWLP_USERDATA));
    if (!state)
        return DefWindowProcW(overlay, message, wParam, lParam);

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT paint { };
        HDC dc = BeginPaint(overlay, &paint);
        RECT client { };
        GetClientRect(overlay, &client);
        const int width = std::max(1L, client.right);
        const int height = std::max(1L, client.bottom);

        HBITMAP& cachedBitmap = overlay == state->sidebarOverlayWindow
            ? state->sidebarOverlayBitmap : state->tabOverviewBitmap;
        SIZE& cachedSize = overlay == state->sidebarOverlayWindow
            ? state->sidebarOverlayBitmapSize : state->tabOverviewBitmapSize;
        bool recreatedBitmap = false;
        if (!cachedBitmap || cachedSize.cx != width || cachedSize.cy != height) {
            if (cachedBitmap)
                DeleteObject(cachedBitmap);
            cachedBitmap = CreateCompatibleBitmap(dc, width, height);
            cachedSize = cachedBitmap ? SIZE { width, height } : SIZE { 0, 0 };
            recreatedBitmap = true;
            if (overlay == state->sidebarOverlayWindow)
                state->sidebarOverlayAnimationFrameReady = false;
            else if (overlay == state->tabOverviewWindow)
                state->tabOverviewAnimationFrameReady = false;
        }

        HDC bufferDC = CreateCompatibleDC(dc);
        const bool reuseSidebarFrame = overlay == state->sidebarOverlayWindow
            && state->sidebarAnimating && state->sidebarOverlayAnimationFrameReady
            && !recreatedBitmap && cachedBitmap;
        const bool reuseOverviewFrame = overlay == state->tabOverviewWindow
            && (((state->tabOverviewAnimating || state->tabOverviewClosing)
                    && state->tabOverviewAnimationFrameReady)
                || (state->sidebarAnimating && state->tabOverviewVisible))
            && !recreatedBitmap && cachedBitmap;
        const bool reuseOverlayFrame = reuseSidebarFrame || reuseOverviewFrame;

        if (bufferDC && cachedBitmap) {
            HGDIOBJ oldBitmap = SelectObject(bufferDC, cachedBitmap);
            if (!reuseOverlayFrame) {
                RECT bufferRect { 0, 0, width, height };
                HBRUSH background = CreateSolidBrush(state->chromeDarkTheme()
                    ? RGB(30, 30, 32) : RGB(240, 241, 244));
                FillRect(bufferDC, &bufferRect, background);
                DeleteObject(background);

                const int saved = SaveDC(bufferDC);
                if (saved) {
                    if (overlay == state->sidebarOverlayWindow) {
                        SetViewportOrgEx(bufferDC, 0, -state->contentTop(), nullptr);
                        state->drawSidebarPanel(bufferDC, true);
                    } else if (overlay == state->tabOverviewWindow) {
                        const RECT area = state->tabOverviewAreaRect();
                        SetViewportOrgEx(bufferDC, -area.left, -state->contentTop(), nullptr);
                        state->drawTabOverviewPage(bufferDC);
                    }
                    RestoreDC(bufferDC, saved);
                }
                if (overlay == state->sidebarOverlayWindow && state->sidebarAnimating)
                    state->sidebarOverlayAnimationFrameReady = true;
                else if (overlay == state->tabOverviewWindow
                    && (state->tabOverviewAnimating || state->tabOverviewClosing))
                    state->tabOverviewAnimationFrameReady = true;
            }

            const bool composeOverviewFrame = overlay == state->tabOverviewWindow
                && (state->tabOverviewClosing || state->tabOverviewAnimating);
            if (composeOverviewFrame) {
                if (!state->tabOverviewFrameBitmap
                    || state->tabOverviewFrameBitmapSize.cx != width
                    || state->tabOverviewFrameBitmapSize.cy != height) {
                    if (state->tabOverviewFrameBitmap)
                        DeleteObject(state->tabOverviewFrameBitmap);
                    state->tabOverviewFrameBitmap = CreateCompatibleBitmap(dc, width, height);
                    state->tabOverviewFrameBitmapSize = state->tabOverviewFrameBitmap
                        ? SIZE { width, height } : SIZE { 0, 0 };
                }

                HDC frameDC = CreateCompatibleDC(dc);
                bool frameComposited = false;
                if (frameDC && state->tabOverviewFrameBitmap) {
                    HGDIOBJ oldFrame = SelectObject(frameDC, state->tabOverviewFrameBitmap);
                    if (BitBlt(frameDC, 0, 0, width, height, bufferDC, 0, 0, SRCCOPY)) {
                        if (state->tabOverviewClosing)
                            state->drawTabOverviewClosingTransition(frameDC);
                        else
                            state->drawTabOverviewOpeningTransition(frameDC);
                        // Present the complete background+snapshot as one screen write.
                        frameComposited = BitBlt(dc, 0, 0, width, height,
                            frameDC, 0, 0, SRCCOPY) != FALSE;
                    }
                    SelectObject(frameDC, oldFrame);
                    DeleteDC(frameDC);
                }

                if (!frameComposited) {
                    // Allocation failure fallback: preserve function rather than
                    // dropping the overview frame, though normal frames use one blit.
                    BitBlt(dc, 0, 0, width, height, bufferDC, 0, 0, SRCCOPY);
                    if (state->tabOverviewClosing)
                        state->drawTabOverviewClosingTransition(dc);
                    else
                        state->drawTabOverviewOpeningTransition(dc);
                }
            } else {
                BitBlt(dc, 0, 0, width, height, bufferDC, 0, 0, SRCCOPY);
            }
            SelectObject(bufferDC, oldBitmap);
            DeleteDC(bufferDC);
        } else {
            if (bufferDC)
                DeleteDC(bufferDC);
            if (overlay == state->sidebarOverlayWindow)
                state->sidebarOverlayAnimationFrameReady = false;
            else if (overlay == state->tabOverviewWindow)
                state->tabOverviewAnimationFrameReady = false;
            const int saved = SaveDC(dc);
            if (saved) {
                if (overlay == state->sidebarOverlayWindow) {
                    SetViewportOrgEx(dc, 0, -state->contentTop(), nullptr);
                    state->drawSidebarPanel(dc, true);
                } else if (overlay == state->tabOverviewWindow) {
                    const RECT area = state->tabOverviewAreaRect();
                    SetViewportOrgEx(dc, -area.left, -state->contentTop(), nullptr);
                    state->drawTabOverviewPage(dc);
                }
                RestoreDC(dc, saved);
            }
            if (overlay == state->tabOverviewWindow) {
                if (state->tabOverviewClosing)
                    state->drawTabOverviewClosingTransition(dc);
                else if (state->tabOverviewAnimating)
                    state->drawTabOverviewOpeningTransition(dc);
            }
        }

        EndPaint(overlay, &paint);
        return 0;
    }

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking { };
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = overlay;
        TrackMouseEvent(&tracking);

        POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (overlay == state->tabOverviewWindow) {
            MapWindowPoints(overlay, state->window, &point, 1);
        } else {
            point.y += state->contentTop();
        }
        if (!state->tabOverviewClosing)
            state->updateToolbarHover(point);
        return 0;
    }

    case WM_MOUSELEAVE:
        state->updateToolbarHover(POINT { -1, -1 });
        return 0;

    case WM_LBUTTONDOWN: {
        POINT point { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (overlay == state->sidebarOverlayWindow) {
            point.y += state->contentTop();
            state->handleSidebarHit(point);
        } else if (overlay == state->tabOverviewWindow && !state->tabOverviewClosing) {
            MapWindowPoints(overlay, state->window, &point, 1);
            state->handleTabOverviewHit(point);
        }
        SetFocus(state->window);
        return 0;
    }

    case WM_KEYDOWN:
        if (state->tabOverviewVisible && wParam == VK_ESCAPE) {
            state->activateTab(state->activeTab);
            return 0;
        }
        break;

    default:
        break;
    }
    return DefWindowProcW(overlay, message, wParam, lParam);
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
    gAuroraAppIcon = createAuroraIcon();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW windowClass { };
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = gAuroraAppIcon;
    windowClass.hIconSm = gAuroraAppIcon;
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = windowClassName;

    if (!RegisterClassExW(&windowClass)) {
        MessageBoxW(nullptr, L"Could not register the Aurora window class.", windowTitle, MB_OK | MB_ICONERROR);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    WNDCLASSEXW overlayClass { };
    overlayClass.cbSize = sizeof(overlayClass);
    overlayClass.lpfnWndProc = overlayWindowProcedure;
    overlayClass.hInstance = instance;
    overlayClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    overlayClass.hbrBackground = nullptr;
    overlayClass.lpszClassName = overlayWindowClassName;
    if (!RegisterClassExW(&overlayClass)) {
        MessageBoxW(nullptr, L"Could not register the Aurora overlay window class.", windowTitle, MB_OK | MB_ICONERROR);
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

    if (gAuroraAppIcon) {
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(gAuroraAppIcon));
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(gAuroraAppIcon));
    }
    // Restore the previous placement when enabled; otherwise respect Windows'
    // requested startup command.
    const bool restoredSavedWindowState = browserState.applySavedWindowState();
    // Keep startup maximization in Aurora's custom geometry state so later
    // restore actions use the same animated path instead of native WS_MAXIMIZE.
    if (!restoredSavedWindowState && showCommand == SW_MAXIMIZE) {
        RECT normalRect { };
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info { };
        info.cbSize = sizeof(info);
        if (GetWindowRect(window, &normalRect) && monitor && GetMonitorInfoW(monitor, &info)) {
            if (SetWindowPos(window, HWND_TOP,
                    info.rcWork.left, info.rcWork.top,
                    info.rcWork.right - info.rcWork.left,
                    info.rcWork.bottom - info.rcWork.top,
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
                browserState.restoredWindowRect = normalRect;
                browserState.lastNormalWindowRect = normalRect;
                browserState.hasLastNormalWindowRect = true;
                browserState.customMaximized = true;
            }
        }
    }

    // Match the DWM border to the selected browser appearance.
    disableNativeWindowBorder(window);

    ShowWindow(window, SW_SHOWNORMAL);
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
        // Browser shortcuts must only be translated for actual key-down messages
        // while a required Ctrl/Alt modifier is held. This keeps ordinary text
        // entry in the address bar from being interpreted as a browser command.
        const bool isShortcutKeyMessage = message.message == WM_KEYDOWN
            || message.message == WM_SYSKEYDOWN;
        const bool hasShortcutModifier = (GetKeyState(VK_CONTROL) & 0x8000) != 0
            || (GetKeyState(VK_MENU) & 0x8000) != 0;
        bool acceleratorHandled = false;
        if (isShortcutKeyMessage && hasShortcutModifier) {
            acceleratorHandled = TranslateAcceleratorW(window, acceleratorTable, &message) != 0;
            if (!acceleratorHandled) {
                for (const auto& browser : gAdditionalBrowserWindows) {
                    if (browser && browser->window
                        && TranslateAcceleratorW(browser->window, acceleratorTable, &message)) {
                        acceleratorHandled = true;
                        break;
                    }
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
