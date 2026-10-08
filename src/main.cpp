#include <windows.h>

#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPageStateClient.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKRetainPtr.h>
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

constexpr UINT kBackButton = 100;
constexpr UINT kForwardButton = 101;
constexpr UINT kReloadButton = 102;
constexpr UINT kHomeButton = 103;
constexpr UINT kMenuButton = 104;
constexpr UINT kAddressBar = 105;
constexpr UINT kNewTabButton = 106;

constexpr UINT kTabButtonBase = 1000;

constexpr UINT kMenuNewTab = 2001;
constexpr UINT kMenuReload = 2002;
constexpr UINT kMenuBack = 2003;
constexpr UINT kMenuForward = 2004;
constexpr UINT kMenuHome = 2005;
constexpr UINT kMenuZoomIn = 2006;
constexpr UINT kMenuZoomOut = 2007;
constexpr UINT kMenuResetZoom = 2008;
constexpr UINT kMenuAbout = 2009;
constexpr UINT kMenuExit = 2010;
constexpr UINT kCloseTabCommand = 2011;

constexpr int kTabBarHeight = 38;
constexpr int kToolbarHeight = 54;

struct BrowserState;

void didChangeIsLoading(const void*);
void didChangeTitle(const void*);
void didChangeActiveURL(const void*);
void didChangeEstimatedProgress(const void*);
void didChangeCanGoBack(const void*);
void didChangeCanGoForward(const void*);

struct TabState {
    BrowserState* browser { nullptr };
    WKRetainPtr<WKViewRef> view;
    HWND tabButton { nullptr };
    std::wstring title { L"New Tab" };
};

std::wstring createString(WKStringRef string)
{
    if (!string)
        return { };

    auto characters = WKStringGetCharactersPtr(string);
    auto length = WKStringGetLength(string);
    return std::wstring(characters, characters + length);
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

std::string percentEncode(const std::wstring& value)
{
    auto utf8 = toUTF8(value);
    static constexpr char hex[] = "0123456789ABCDEF";

    std::string result;
    result.reserve(utf8.size() * 3);

    for (unsigned char byte : utf8) {
        if ((byte >= 'a' && byte <= 'z')
            || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9')
            || byte == '-'
            || byte == '_'
            || byte == '.'
            || byte == '~') {
            result.push_back(static_cast<char>(byte));
            continue;
        }

        result.push_back('%');
        result.push_back(hex[(byte >> 4) & 0x0F]);
        result.push_back(hex[byte & 0x0F]);
    }

    return result;
}

WKRetainPtr<WKURLRef> createWKURL(const std::wstring& value)
{
    return adoptWK(WKURLCreateWithUTF8CString(toUTF8(value).c_str()));
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

double windowScaleFactor(HWND window)
{
    UINT dpi = 96;

    using GetDpiForWindowFunction = UINT (WINAPI*)(HWND);
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
            GetProcAddress(user32, "GetDpiForWindow")
        );
        if (getDpiForWindow)
            dpi = getDpiForWindow(window);
    }

    return static_cast<double>(dpi) / 96.0;
}

LRESULT CALLBACK addressBarProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

BrowserState* browserStateFromWindow(HWND window)
{
    return reinterpret_cast<BrowserState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

struct BrowserState {
    HWND window { nullptr };

    HWND backButton { nullptr };
    HWND forwardButton { nullptr };
    HWND reloadButton { nullptr };
    HWND homeButton { nullptr };
    HWND menuButton { nullptr };
    HWND addressBar { nullptr };
    HWND newTabButton { nullptr };

    WNDPROC addressBarOriginalProcedure { nullptr };

    HMENU menu { nullptr };

    HFONT uiFont { nullptr };
    HBRUSH toolbarBrush { nullptr };
    HBRUSH addressBrush { nullptr };

    WKRetainPtr<WKWebsiteDataStoreConfigurationRef> websiteDataStoreConfiguration;
    WKRetainPtr<WKWebsiteDataStoreRef> websiteDataStore;
    WKRetainPtr<WKContextConfigurationRef> contextConfiguration;
    WKRetainPtr<WKContextRef> context;
    WKRetainPtr<WKPreferencesRef> preferences;
    WKRetainPtr<WKPageConfigurationRef> pageConfiguration;

    std::vector<std::unique_ptr<TabState>> tabs;
    size_t activeTab { 0 };

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
        toolbarBrush = CreateSolidBrush(RGB(244, 247, 248));
        addressBrush = CreateSolidBrush(RGB(255, 255, 255));

        createChrome();
        createMenu();

        if (!addTab(true, true))
            return false;

        resizeChrome();
        updateNavigationButtons();
        return true;
    }

    ~BrowserState()
    {
        if (addressBar && addressBarOriginalProcedure)
            SetWindowLongPtrW(addressBar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(addressBarOriginalProcedure));

        if (menu)
            DestroyMenu(menu);
        if (uiFont)
            DeleteObject(uiFont);
        if (toolbarBrush)
            DeleteObject(toolbarBrush);
        if (addressBrush)
            DeleteObject(addressBrush);
    }

    TabState* active()
    {
        if (tabs.empty())
            return nullptr;
        if (activeTab >= tabs.size())
            activeTab = tabs.size() - 1;
        return tabs[activeTab].get();
    }

    const TabState* active() const
    {
        if (tabs.empty() || activeTab >= tabs.size())
            return nullptr;
        return tabs[activeTab].get();
    }

    void createChrome()
    {
        backButton = createButton(L"‹", kBackButton);
        forwardButton = createButton(L"›", kForwardButton);
        reloadButton = createButton(L"↻", kReloadButton);
        homeButton = createButton(L"⌂", kHomeButton);

        addressBar = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddressBar)),
            GetModuleHandleW(nullptr),
            nullptr
        );

        menuButton = createButton(L"⋯", kMenuButton);
        newTabButton = createButton(L"+", kNewTabButton);

        SendMessageW(addressBar, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        addressBarOriginalProcedure = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(addressBar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(addressBarProcedure))
        );
        SetWindowLongPtrW(addressBar, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        for (HWND control : { backButton, forwardButton, reloadButton, homeButton, menuButton, newTabButton })
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    }

    HWND createButton(const wchar_t* text, UINT id)
    {
        return CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr),
            nullptr
        );
    }

    void createMenu()
    {
        menu = CreatePopupMenu();
        if (!menu)
            return;

        AppendMenuW(menu, MF_STRING, kMenuNewTab, L"New Tab");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuBack, L"Back");
        AppendMenuW(menu, MF_STRING, kMenuForward, L"Forward");
        AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload");
        AppendMenuW(menu, MF_STRING, kMenuHome, L"Home");

        HMENU zoomMenu = CreatePopupMenu();
        AppendMenuW(zoomMenu, MF_STRING, kMenuZoomIn, L"Zoom In");
        AppendMenuW(zoomMenu, MF_STRING, kMenuZoomOut, L"Zoom Out");
        AppendMenuW(zoomMenu, MF_STRING, kMenuResetZoom, L"Reset Zoom");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(zoomMenu), L"Page Zoom");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuAbout, L"About Aurora");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"Quit Aurora");
    }

    void resizeChrome() const
    {
        if (!window)
            return;

        RECT clientRect { };
        if (!GetClientRect(window, &clientRect))
            return;

        int width = clientRect.right - clientRect.left;
        int dpi = 96;
        using GetDpiForWindowFunction = UINT (WINAPI*)(HWND);
        HMODULE user32 = LoadLibraryW(L"user32.dll");
        if (user32) {
            auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
                GetProcAddress(user32, "GetDpiForWindow")
            );
            if (getDpiForWindow)
                dpi = static_cast<int>(getDpiForWindow(window));
        }

        auto scale = [dpi](int logicalPixels) {
            return MulDiv(logicalPixels, dpi, 96);
        };

        int tabHeight = scale(kTabBarHeight);
        int toolbarHeight = scale(kToolbarHeight);
        int buttonWidth = scale(38);
        int gap = scale(4);
        int addressLeft = gap * 2 + buttonWidth * 4;
        int menuWidth = scale(38);
        int newTabWidth = scale(38);
        int addressRight = width - gap * 2 - menuWidth;

        MoveWindow(backButton, gap, tabHeight + gap, buttonWidth, toolbarHeight - gap * 2, TRUE);
        MoveWindow(forwardButton, gap + buttonWidth, tabHeight + gap, buttonWidth, toolbarHeight - gap * 2, TRUE);
        MoveWindow(reloadButton, gap + buttonWidth * 2, tabHeight + gap, buttonWidth, toolbarHeight - gap * 2, TRUE);
        MoveWindow(homeButton, gap + buttonWidth * 3, tabHeight + gap, buttonWidth, toolbarHeight - gap * 2, TRUE);

        MoveWindow(
            addressBar,
            addressLeft,
            tabHeight + gap,
            std::max(scale(180), addressRight - addressLeft - gap),
            toolbarHeight - gap * 2,
            TRUE
        );

        MoveWindow(menuButton, addressRight, tabHeight + gap, menuWidth, toolbarHeight - gap * 2, TRUE);

        int tabButtonWidth = tabs.empty()
            ? width - gap
            : std::max(scale(110), std::min(scale(220), (width - newTabWidth - gap * (static_cast<int>(tabs.size()) + 2)) / static_cast<int>(tabs.size())));

        for (size_t i = 0; i < tabs.size(); ++i) {
            int left = gap + static_cast<int>(i) * (tabButtonWidth + gap);
            MoveWindow(
                tabs[i]->tabButton,
                left,
                gap / 2,
                tabButtonWidth,
                tabHeight - gap,
                TRUE
            );
        }

        MoveWindow(
            newTabButton,
            width - newTabWidth - gap,
            gap / 2,
            newTabWidth,
            tabHeight - gap,
            TRUE
        );

        int contentTop = tabHeight + toolbarHeight;
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (!tabs[i]->view)
                continue;

            HWND webViewWindow = WKViewGetWindow(tabs[i]->view.get());
            if (!webViewWindow)
                continue;

            MoveWindow(
                webViewWindow,
                0,
                contentTop,
                width,
                std::max(0, clientRect.bottom - contentTop),
                TRUE
            );
        }
    }

    bool addTab(bool selectTab, bool useStartPage)
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
        tabs[index]->tabButton = createButton(tabs[index]->title.c_str(), static_cast<UINT>(kTabButtonBase + index));

        if (selectTab)
            activeTab = index;

        if (useStartPage)
            loadStartPage(*tabs[index]);
        else
            loadURL(*tabs[index], L"about:blank");

        rebuildTabButtons();
        updateNavigationButtons();
        resizeChrome();
        return true;
    }

    void rebuildTabButtons()
    {
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (!tabs[i]->tabButton)
                tabs[i]->tabButton = createButton(tabs[i]->title.c_str(), static_cast<UINT>(kTabButtonBase + i));
            else {
                SetWindowTextW(tabs[i]->tabButton, tabs[i]->title.c_str());
                EnableWindow(tabs[i]->tabButton, i != activeTab);
            }

            if (uiFont)
                SendMessageW(tabs[i]->tabButton, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        }
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
:root { color-scheme: light; font-family: "Segoe UI", sans-serif; }
body { margin:0; min-height:100vh; display:flex; align-items:center; justify-content:center; background:linear-gradient(180deg,#f7fbfc 0%,#edf5f6 100%); color:#142126; }
.wrap { text-align:center; width:min(720px,90vw); }
.logo { width:92px; height:92px; margin:0 auto 24px; border-radius:50%; display:grid; place-items:center; background:conic-gradient(from 210deg,#0d6f88,#27c9aa,#b3f17b,#18b8dc,#0d6f88); box-shadow:0 16px 40px rgba(20,130,150,.22); position:relative; overflow:hidden; }
.logo:before { content:"a"; color:white; font-size:68px; font-weight:700; line-height:1; font-family:"Segoe UI"; text-shadow:0 4px 14px rgba(0,0,0,.12); }
h1 { font-size:38px; letter-spacing:.24em; margin:0 0 10px; font-weight:500; }
p { color:#5f7075; margin:0 0 28px; font-size:15px; }
.search { width:100%; box-sizing:border-box; padding:16px 20px; border:1px solid #d7e2e4; border-radius:22px; background:#fff; font-size:16px; outline:none; box-shadow:0 8px 30px rgba(27,70,80,.08); }
.search:focus { border-color:#55aeba; box-shadow:0 10px 34px rgba(27,150,160,.16); }
.hints { margin-top:22px; font-size:13px; color:#768a90; }
</style>
</head>
<body>
<div class="wrap">
<div class="logo"></div>
<h1>AURORA</h1>
<p>A WebKit browser for Windows.</p>
<input class="search" autofocus placeholder="Search the web or enter an address">
<div class="hints">Type a URL or a search query in the toolbar above.</div>
</div>
</body>
</html>
)HTML";

        auto baseURL = createWKURL(L"about:blank");
        auto page = WKViewGetPage(tab.view.get());
        auto htmlString = adoptWK(WKStringCreateWithUTF8CString(html));
        WKPageLoadHTMLString(page, htmlString.get(), baseURL.get());
        SetWindowTextW(addressBar, L"");
    }

    void loadURL(TabState& tab, const std::wstring& url)
    {
        auto page = WKViewGetPage(tab.view.get());
        auto wkURL = createWKURL(url);
        if (page && wkURL)
            WKPageLoadURL(page, wkURL.get());
    }

    void navigateInput(const std::wstring& input)
    {
        auto cleaned = input;
        while (!cleaned.empty() && (cleaned.front() == L' ' || cleaned.front() == L'\t'))
            cleaned.erase(cleaned.begin());
        while (!cleaned.empty() && (cleaned.back() == L' ' || cleaned.back() == L'\t'))
            cleaned.pop_back();

        if (cleaned.empty())
            return;

        if (cleaned.find(L"://") != std::wstring::npos
            || cleaned.rfind(L"about:", 0) == 0
            || cleaned.rfind(L"file:", 0) == 0) {
            if (auto* tab = active())
                loadURL(*tab, cleaned);
            return;
        }

        auto encodedQuery = percentEncode(cleaned);
        auto searchURL = std::wstring(L"https://www.google.com/search?q=") + std::wstring(encodedQuery.begin(), encodedQuery.end());
        if (auto* tab = active())
            loadURL(*tab, searchURL);
    }

    void focusAddressBar()
    {
        SetFocus(addressBar);
        SendMessageW(addressBar, EM_SETSEL, 0, -1);
    }

    void goBack()
    {
        if (auto* tab = active()) {
            auto page = WKViewGetPage(tab->view.get());
            if (page && WKPageCanGoBack(page))
                WKPageGoBack(page);
        }
    }

    void goForward()
    {
        if (auto* tab = active()) {
            auto page = WKViewGetPage(tab->view.get());
            if (page && WKPageCanGoForward(page))
                WKPageGoForward(page);
        }
    }

    void reload()
    {
        if (auto* tab = active())
            WKPageReload(WKViewGetPage(tab->view.get()));
    }

    void stopOrReload()
    {
        if (auto* tab = active()) {
            auto page = WKViewGetPage(tab->view.get());
            if (!page)
                return;

            if (WKPageGetEstimatedProgress(page) < 1.0)
                WKPageStopLoading(page);
            else
                WKPageReload(page);
        }
    }

    void goHome()
    {
        if (auto* tab = active())
            loadStartPage(*tab);
    }

    void activateTab(size_t index)
    {
        if (index >= tabs.size())
            return;

        activeTab = index;

        for (size_t i = 0; i < tabs.size(); ++i) {
            HWND webViewWindow = WKViewGetWindow(tabs[i]->view.get());
            if (webViewWindow)
                ShowWindow(webViewWindow, i == activeTab ? SW_SHOW : SW_HIDE);

            if (tabs[i]->tabButton)
                EnableWindow(tabs[i]->tabButton, i != activeTab);
        }

        if (auto* tab = active()) {
            auto url = createString(adoptWK(WKPageCopyActiveURL(WKViewGetPage(tab->view.get()))).get());
            if (url != L"about:blank")
                SetWindowTextW(addressBar, url.c_str());
            else
                SetWindowTextW(addressBar, L"");
            SetFocus(WKViewGetWindow(tab->view.get()));
        }

        updateNavigationButtons();
    }

    void closeActiveTab()
    {
        if (tabs.empty())
            return;

        if (tabs.size() == 1) {
            PostMessageW(window, WM_CLOSE, 0, 0);
            return;
        }

        size_t oldActive = activeTab;
        tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(oldActive));

        if (activeTab >= tabs.size())
            activeTab = tabs.size() - 1;

        rebuildTabButtons();
        activateTab(activeTab);
        resizeChrome();
    }

    void updateNavigationButtons() const
    {
        auto* tab = active();
        if (!tab)
            return;

        auto page = WKViewGetPage(tab->view.get());
        if (!page)
            return;

        EnableWindow(backButton, WKPageCanGoBack(page));
        EnableWindow(forwardButton, WKPageCanGoForward(page));

        bool loading = WKPageGetEstimatedProgress(page) < 1.0;
        SetWindowTextW(reloadButton, loading ? L"×" : L"↻");
    }

    void updateAddressBar(TabState& tab)
    {
        if (&tab != active())
            return;

        auto page = WKViewGetPage(tab.view.get());
        auto url = createString(adoptWK(WKPageCopyActiveURL(page)).get());

        if (url == L"about:blank")
            SetWindowTextW(addressBar, L"");
        else
            SetWindowTextW(addressBar, url.c_str());

        SetWindowTextW(window, (tab.title.empty() ? L"Aurora" : tab.title + L" — Aurora").c_str());
    }

    void updateTitle(TabState& tab)
    {
        auto page = WKViewGetPage(tab.view.get());
        auto title = createString(adoptWK(WKPageCopyTitle(page)).get());

        if (!title.empty())
            tab.title = title;
        else
            tab.title = L"New Tab";

        if (tab.tabButton)
            SetWindowTextW(tab.tabButton, tab.title.c_str());

        if (&tab == active())
            SetWindowTextW(window, (tab.title + L" — Aurora").c_str());
    }

    void showMenu()
    {
        if (!menuButton || !menu)
            return;

        RECT rect { };
        GetWindowRect(menuButton, &rect);
        TrackPopupMenu(
            menu,
            TPM_RIGHTALIGN | TPM_TOPALIGN,
            rect.right,
            rect.bottom,
            0,
            window,
            nullptr
        );
    }

    void handleCommand(UINT id)
    {
        if (id >= kTabButtonBase && id < kTabButtonBase + tabs.size()) {
            activateTab(id - kTabButtonBase);
            return;
        }

        switch (id) {
        case kBackButton:
        case kMenuBack:
            goBack();
            break;
        case kForwardButton:
        case kMenuForward:
            goForward();
            break;
        case kReloadButton:
        case kMenuReload:
            stopOrReload();
            break;
        case kHomeButton:
        case kMenuHome:
            goHome();
            break;
        case kMenuButton:
            showMenu();
            break;
        case kNewTabButton:
        case kMenuNewTab:
            addTab(true, true);
            activateTab(activeTab);
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
                WKPageSetPageZoomFactor(page, WKPageGetPageZoomFactor(page) * 0.9);
            }
            break;
        case kMenuResetZoom:
            if (auto* tab = active())
                WKPageSetPageZoomFactor(WKViewGetPage(tab->view.get()), 1.0);
            break;
        case kMenuAbout:
            MessageBoxW(
                window,
                L"Aurora\n\nA WebKit browser for Windows.\n\nBuilt as an independent project with original Aurora branding.",
                L"About Aurora",
                MB_OK | MB_ICONINFORMATION
            );
            break;
        case kMenuExit:
            PostMessageW(window, WM_CLOSE, 0, 0);
            break;
        case kCloseTabCommand:
            closeActiveTab();
            break;
        default:
            break;
        }

        updateNavigationButtons();
    }
};

LRESULT CALLBACK addressBarProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<BrowserState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state || !state->addressBarOriginalProcedure)
        return DefWindowProcW(window, message, wParam, lParam);

    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        wchar_t buffer[4096] { };
        GetWindowTextW(window, buffer, static_cast<int>(std::size(buffer)));
        state->navigateInput(buffer);
        return 0;
    }

    if (message == WM_SETFOCUS) {
        LRESULT result = CallWindowProcW(state->addressBarOriginalProcedure, window, message, wParam, lParam);
        SendMessageW(window, EM_SETSEL, 0, -1);
        return result;
    }

    return CallWindowProcW(state->addressBarOriginalProcedure, window, message, wParam, lParam);
}

void didChangeIsLoading(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        tab->browser->updateNavigationButtons();
}

void didChangeTitle(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        tab->browser->updateTitle(*tab);
}

void didChangeActiveURL(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        tab->browser->updateAddressBar(*tab);
}

void didChangeEstimatedProgress(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        tab->browser->updateNavigationButtons();
}

void didChangeCanGoBack(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        tab->browser->updateNavigationButtons();
}

void didChangeCanGoForward(const void* clientInfo)
{
    auto* tab = const_cast<TabState*>(static_cast<const TabState*>(clientInfo));
    if (tab && tab->browser)
        tab->browser->updateNavigationButtons();
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

    auto* state = browserStateFromWindow(window);

    switch (message) {
    case WM_CREATE:
        if (!state || !state->initialize(window)) {
            MessageBoxW(
                window,
                L"WebKit could not initialize the Aurora browser.",
                windowTitle,
                MB_OK | MB_ICONERROR
            );
            return -1;
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint { };
        HDC dc = BeginPaint(window, &paint);

        RECT clientRect { };
        GetClientRect(window, &clientRect);

        int dpi = 96;
        using GetDpiForWindowFunction = UINT (WINAPI*)(HWND);
        HMODULE user32 = LoadLibraryW(L"user32.dll");
        if (user32) {
            auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
                GetProcAddress(user32, "GetDpiForWindow")
            );
            if (getDpiForWindow)
                dpi = static_cast<int>(getDpiForWindow(window));
        }

        int tabHeight = MulDiv(kTabBarHeight, dpi, 96);
        int toolbarHeight = MulDiv(kToolbarHeight, dpi, 96);

        HBRUSH toolbarBrush = state && state->toolbarBrush
            ? state->toolbarBrush
            : GetSysColorBrush(COLOR_WINDOW);

        RECT tabsRect = clientRect;
        tabsRect.bottom = tabHeight;
        FillRect(dc, &tabsRect, toolbarBrush);

        RECT toolbarRect = clientRect;
        toolbarRect.top = tabHeight;
        toolbarRect.bottom = tabHeight + toolbarHeight;
        FillRect(dc, &toolbarRect, toolbarBrush);

        EndPaint(window, &paint);
        return 0;
    }

    case WM_CTLCOLOREDIT:
        if (state && reinterpret_cast<HWND>(lParam) == state->addressBar) {
            auto dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(35, 45, 50));
            SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(state->addressBrush);
        }
        break;

    case WM_SIZE:
        if (state)
            state->resizeChrome();
        return 0;

    case WM_DPICHANGED:
        if (state) {
            for (auto& tab : state->tabs) {
                if (tab->view) {
                    auto page = WKViewGetPage(tab->view.get());
                    if (page)
                        WKPageSetCustomBackingScaleFactor(page, windowScaleFactor(window));
                }
            }
            state->resizeChrome();
        }
        return 0;

    case WM_COMMAND:
        if (state && (HIWORD(wParam) == 0 || HIWORD(wParam) == BN_CLICKED))
            state->handleCommand(LOWORD(wParam));
        return 0;

    case WM_DESTROY:
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
    enableDpiAwareness();

    WNDCLASSEXW windowClass { };
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
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
        { FCONTROL, 'L', kAddressBar },
        { FCONTROL, 'R', kReloadButton },
        { FCONTROL, 'T', kNewTabButton },
        { FCONTROL, 'W', kCloseTabCommand },
        { FALT, VK_LEFT, kBackButton },
        { FALT, VK_RIGHT, kForwardButton }
    };
    HACCEL acceleratorTable = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message { };
    for (;;) {
        BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == 0)
            break;
        if (result == -1)
            break;

        if (!TranslateAcceleratorW(window, acceleratorTable, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (acceleratorTable)
        DestroyAcceleratorTable(acceleratorTable);

    return static_cast<int>(message.wParam);
}
