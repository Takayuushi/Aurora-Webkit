#include <windows.h>

#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKRetainPtr.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>

namespace {

constexpr wchar_t windowClassName[] = L"AuroraWindow";
constexpr wchar_t windowTitle[] = L"Aurora";
constexpr char initialURL[] = "https://www.google.com";

struct BrowserState {
    HWND window { nullptr };

    // Keep the configuration objects alive for the lifetime of the view.
    WKRetainPtr<WKWebsiteDataStoreConfigurationRef> websiteDataStoreConfiguration;
    WKRetainPtr<WKWebsiteDataStoreRef> websiteDataStore;
    WKRetainPtr<WKContextConfigurationRef> contextConfiguration;
    WKRetainPtr<WKContextRef> context;
    WKRetainPtr<WKPreferencesRef> preferences;
    WKRetainPtr<WKPageConfigurationRef> pageConfiguration;
    WKRetainPtr<WKViewRef> view;

    bool initialize(HWND parentWindow)
    {
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

        RECT initialRect { };
        view = adoptWK(WKViewCreate(initialRect, pageConfiguration.get(), parentWindow));
        if (!view)
            return false;

        WKViewSetIsInWindow(view.get(), true);

        WKPageRef page = WKViewGetPage(view.get());
        if (!page)
            return false;

        auto url = adoptWK(WKURLCreateWithUTF8CString(initialURL));
        if (!url)
            return false;

        WKPageLoadURL(page, url.get());
        resizeView();
        return true;
    }

    void resizeView() const
    {
        if (!window || !view)
            return;

        HWND webViewWindow = WKViewGetWindow(view.get());
        if (!webViewWindow)
            return;

        RECT clientRect { };
        if (!GetClientRect(window, &clientRect))
            return;

        MoveWindow(
            webViewWindow,
            0,
            0,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top,
            TRUE
        );
    }
};

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
            MessageBoxW(
                window,
                L"WebKit could not initialize the browser view.",
                windowTitle,
                MB_OK | MB_ICONERROR
            );
            return -1;
        }
        return 0;

    case WM_SIZE:
        if (state)
            state->resizeView();
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
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1200,
        800,
        nullptr,
        nullptr,
        instance,
        &browserState
    );
    if (!window) {
        MessageBoxW(nullptr, L"Could not create the Aurora window.", windowTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message { };
    for (;;) {
        BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == 0)
            return static_cast<int>(message.wParam);
        if (result == -1)
            return 1;

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
