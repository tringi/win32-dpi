#include <Windows.h>
#include <Uxtheme.h>
#include <vssym32.h>

#include <CommCtrl.h>
#include <VersionHelpers.h>

#include <cstdint>
#include <cstdio>
#include <new>

extern "C" IMAGE_DOS_HEADER __ImageBase;
extern "C" const IID IID_IImageList;

// APIs we need, but are not available in all supported OS'
//  - if support for pre-1607 releases of Windows 10 is not required, the code can be a lot simpler

int (WINAPI * ptrGetSystemMetricsForDpi) (int, UINT) = NULL;
BOOL (WINAPI * ptrEnableNonClientDpiScaling) (HWND) = NULL;
UINT (WINAPI * pfnGetDpiForSystem) () = NULL;
UINT (WINAPI * pfnGetDpiForWindow) (HWND) = NULL;
BOOL (WINAPI * ptrAreDpiAwarenessContextsEqual) (DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT) = NULL;
DPI_AWARENESS_CONTEXT (WINAPI * ptrGetWindowDpiAwarenessContext) (HWND) = NULL;
HRESULT (WINAPI * ptrLoadIconWithScaleDown) (HINSTANCE, PCWSTR, int, int, HICON *) = NULL;

// Convenient loading function, see WinMain
//  - simplified version of https://github.com/tringi/emphasize/blob/master/Windows/Windows_Symbol.hpp

template <typename P>
bool Symbol (HMODULE h, P & pointer, const char * name) {
    if (P p = reinterpret_cast <P> (GetProcAddress (h, name))) {
        pointer = p;
        return true;
    } else
        return false;
}
template <typename P>
bool Symbol (HMODULE h, P & pointer, USHORT index) {
    return Symbol (h, pointer, MAKEINTRESOURCEA (index));
}

// Generalized DPI retrieval
//  - GetDpiFor(System/Window) available since 1607 / LTSB2016 / Server 2016
//  - GetDeviceCaps is classic way, working way back to XP
//
UINT GetDPI (HWND hWnd) {
    if (hWnd != NULL) {
        if (pfnGetDpiForWindow)
            return pfnGetDpiForWindow (hWnd);
    } else {
        if (pfnGetDpiForSystem)
            return pfnGetDpiForSystem ();
    }
    if (HDC hDC = GetDC (hWnd)) {
        auto dpi = GetDeviceCaps (hDC, LOGPIXELSX);
        ReleaseDC (hWnd, hDC);
        return dpi;
    } else
        return USER_DEFAULT_SCREEN_DPI;
}

// TextScale
//  - singleton tracking the per-user "Settings > Accessibility > Text size" feature for UWP Apps
//  - there is no documented Win32 API to get this value, so we read it from registry
//  - we craft this notification facility because:
//     - the OS doesn't always broadcast WM_SETTINGCHANGE message when the scale factor changes
//     - the "Accessibility" key and "TextScaleFactor" value may not exist at first
//  - there is no destructor, the object lives for the lifetime of the process, OS cleans up
//
class TextScale {
    HKEY    hKey = NULL; // HKCU\SOFTWARE\Microsoft[\Accessibility]
    bool    parent = false; // if true, we are waiting for Accessibilty subkey to be created first

public:
    DWORD   current = 100;
    HANDLE  hEvent = NULL;

public:
    bool Initialize () {
        if (IsWindows10OrGreater ()) { // TODO: which build?
            this->hEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
            if (this->hEvent)
                return this->ReOpenKeys ();
        }
        return false;
    }

    // OnEvent
    //  - should be called whenever this->hEvent gets signalled
    //  - returns 'true' if the scale factor might have changed, and application should redraw the GUI
    //
    bool OnEvent () {
        if (this->parent) {
            // "Accessibility" subkey might have been created, try to acces it again
            if (this->ReOpenKeys ()) {
                // if the subkey was created, we now have new scale factor, so report that as change
                return this->parent == false;
            } else
                return false;

        } else {
            bool changed = false;
            // some value inside "Accessibility" subkey has changed, see if it was "TextScaleFactor"
            auto updated = this->GetCurrentTextScaleFactor ();
            if (this->current != updated) {
                this->current = updated;
                changed = true;
            }
            // re-register for next event
            RegNotifyChangeKeyValue (this->hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, this->hEvent, TRUE);
            return changed;
        }
    }

    // Apply 
    //  - adjusts font height according to current text scale factor
    //  - NOTE: if all fonts are to be scaled, this can be called from Window::Font::update
    //
    void Apply (LOGFONT & lf) const {
        lf.lfHeight = MulDiv (lf.lfHeight, this->current, 100);
    }

private:
    bool ReOpenKeys () {
        if (this->hKey) {
            RegCloseKey (this->hKey);
            this->hKey = NULL;
        }
        if (RegOpenKeyEx (HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Accessibility", 0, KEY_NOTIFY | KEY_QUERY_VALUE, &this->hKey) == ERROR_SUCCESS) {
            if (RegNotifyChangeKeyValue (this->hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, this->hEvent, TRUE) == ERROR_SUCCESS) {
                this->parent = false;
                this->current = this->GetCurrentTextScaleFactor ();
                return true;
            }
        } else {
            if (RegOpenKeyEx (HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft", 0, KEY_NOTIFY, &this->hKey) == ERROR_SUCCESS) {
                if (RegNotifyChangeKeyValue (this->hKey, FALSE, REG_NOTIFY_CHANGE_NAME, this->hEvent, TRUE) == ERROR_SUCCESS) {
                    this->parent = true;
                    return true;
                }
            }
        }
        return false;
    }

    DWORD GetCurrentTextScaleFactor () const {
        DWORD scale;
        DWORD cb = sizeof scale;

        if ((this->parent == false) && (this->hKey != NULL) && (RegQueryValueEx (this->hKey, L"TextScaleFactor", NULL, NULL, (LPBYTE) &scale, &cb) == ERROR_SUCCESS)) {
            return scale;
        } else
            return 100;
    }

} TextScale;

// Most (hopefully) reliable way to detect if v2 scaling is imposed on the window
//  - uxtheme Get... APIs return per-window scaled values only if this yields true, otherwise do: dpi * value / dpiSystem
//  - NOTE: GetThemeFont is affected, GetThemeSysFont is not (and still needs to be adjusted)
//
bool AreDpiApisScaled (HWND hWnd) {
    if (ptrGetWindowDpiAwarenessContext && ptrAreDpiAwarenessContextsEqual) {
        return ptrAreDpiAwarenessContextsEqual (ptrGetWindowDpiAwarenessContext (hWnd), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else
        return false;
}

enum IconSize {
    SmallIconSize = 0,
    StartIconSize,
    LargeIconSize,
    ShellIconSize,
    JumboIconSize,
    IconSizesCount
};

HICON LoadBestIcon (HMODULE hModule, LPCWSTR resource, SIZE size) {
    HICON hNewIcon = NULL;
    if (size.cx > 256) size.cx = 256;
    if (size.cy > 256) size.cy = 256;

    if (ptrLoadIconWithScaleDown) {
        if (ptrLoadIconWithScaleDown (hModule, resource, size.cx, size.cy, &hNewIcon) != S_OK) {
            hNewIcon = NULL;
        }
    }
    if (hNewIcon)
        return hNewIcon;
    else
        return (HICON) LoadImage (hModule, resource, IMAGE_ICON, size.cx, size.cy, LR_DEFAULTCOLOR);
}

struct Window {
    const HWND hWnd;
private:
    long dpi = 96;
    int  metrics [SM_CMETRICS] = { 0 };
    HCURSOR cursor = NULL;

    struct {
        HICON standard [IconSizesCount] = { NULL };

        // small cache for icons of different DPI

        struct PerDpiIcon {
            WPARAM type = 0;
            LPARAM dpi = 0;
            HICON  icon = NULL;
        } dpi_cache [16];

        struct {
            bool         found;
            PerDpiIcon * icon;

        } find_in_dpi_cache (WPARAM type, LPARAM dpi) {
            for (auto & icon : this->dpi_cache) {
                if ((icon.type == type) && (icon.dpi == dpi))
                    return { true, &icon };

                if (icon.dpi == 0)
                    return { false, &icon };
            }
            return { false, nullptr };
        }
    } icons;

    explicit Window (HWND hWnd)
        : hWnd (hWnd)
        , dpi (GetDPI (hWnd)) {};

    // GetIconMetrics
    //  - we want crisp icons wherever possible
    //  - including the larger sizes is just flexing
    //
    SIZE GetIconMetrics (IconSize size, UINT dpiSystem = 0) {
        switch (size) {
            case SmallIconSize:
                return { metrics [SM_CXSMICON], metrics [SM_CYSMICON] };
            case StartIconSize:
                return {
                    (metrics [SM_CXICON] + metrics [SM_CXSMICON]) / 2,
                    (metrics [SM_CYICON] + metrics [SM_CYSMICON]) / 2
                };
            case LargeIconSize:
            default:
                return { metrics [SM_CXICON], metrics [SM_CYICON] };

            case ShellIconSize:
            case JumboIconSize:
                if (dpiSystem == 0) {
                    dpiSystem = GetDPI (NULL);
                }
                if (IsWindowsVistaOrGreater () || (size == ShellIconSize)) { // XP doesn't have Jumbo
                    if (HMODULE hShell32 = GetModuleHandle (L"SHELL32")) {
                        HRESULT (WINAPI * ptrSHGetImageList) (int, const GUID &, void **) = NULL;

                        if (IsWindowsVistaOrGreater ()) {
                            Symbol (hShell32, ptrSHGetImageList, "SHGetImageList");
                        } else {
                            Symbol (hShell32, ptrSHGetImageList, 727);
                        }
                        if (ptrSHGetImageList) {
                            HIMAGELIST list;
                            if (ptrSHGetImageList ((size == JumboIconSize) ? SHIL_JUMBO : SHIL_EXTRALARGE,
                                                   IID_IImageList, (void **) &list) == S_OK) {
                                int cx, cy;
                                if (ImageList_GetIconSize (list, &cx, &cy)) {
                                    switch (size) {
                                        case ShellIconSize: return { long (cx * this->dpi / dpiSystem), long (cy * this->dpi / dpiSystem) };
                                        case JumboIconSize: return { long (cx * this->dpi / 96), long (cy * this->dpi / 96) };
                                    }
                                }
                            }
                        }
                    }
                }
                switch (size) {
                    default:
                    case ShellIconSize: return { long (48 * this->dpi / dpiSystem), long (48 * this->dpi / dpiSystem) };
                    case JumboIconSize: return { long (256 * this->dpi / 96), long (256 * this->dpi / 96) };
                }
        }
    }

    // MapIconSize
    //  - selecting proper IconSize from WM_GETICON/WM_SETICON wParam
    //  - using proper size for Windows 10 taskbar
    //
    IconSize MapIconSize (WPARAM type) {
        switch (type) {
            case ICON_BIG:

                // Windows 10 Taskbar icons are not 32x32, but 24x24 (on 96 DPI)
                //  - note that using 24x24 for ICON_BIG works only if the application isn't pinned,
                //    then the Shell will load 32x32 and scale it down despite 24x24 being available;
                //    and surprisingly pinning it explicitly with 24x24 icon won't work either, such
                //    icon will be scaled up to 32x32 and then down to 24x24 resulting in blurry mess

                if (IsWindows10OrGreater ()) {
                    return StartIconSize;
                } else {
                    return LargeIconSize;
                }
            case ICON_SMALL:
            case ICON_SMALL2:
                return SmallIconSize;
            default:
                return LargeIconSize;
        }
    }

    // RefreshVisualMetrics
    //  - enables us to use 'this->metrics [SM_xxx]' whenever we need some metrics
    //    instead of calling the function at every spot
    //  - some values are not actually metrics (exercise for the reader)
    //
    LRESULT RefreshVisualMetrics (UINT dpiSystem = GetDPI (NULL)) {
        if (ptrGetSystemMetricsForDpi) {
            for (auto i = 0; i != sizeof metrics / sizeof metrics [0]; ++i) {
                this->metrics [i] = ptrGetSystemMetricsForDpi (i, this->dpi);
            }
        } else {
            for (auto i = 0; i != sizeof metrics / sizeof metrics [0]; ++i) {
                this->metrics [i] = this->dpi * GetSystemMetrics (i) / dpiSystem;
            }
        }
        return 0;
    }

    // Font
    //  - somehow like this we need to store font handles to release on WM_THEMECHANGED or WM_DPICHANGED
    //  - and we need to remember pixel height to use when repositioning controls on window resize/restore
    //
    struct Font {
        HFONT handle = NULL;
        long  height = 0;

        ~Font () {
            if (this->handle != NULL) {
                DeleteObject (this->handle);
            }
        }
        bool update (LOGFONT lf) {
            if (lf.lfHeight > 0) {
                this->height = lf.lfHeight;
            } else {
                this->height = 96 * -lf.lfHeight / 72;
            }
            if (auto hNewFont = CreateFontIndirect (&lf)) {
                if (this->handle != NULL) {
                    DeleteObject (this->handle);
                }
                this->handle = hNewFont;
                return true;
            } else {
                if (this->handle == NULL) {
                    this->handle = (HFONT) GetStockObject (DEFAULT_GUI_FONT);
                }
                return false;
            }
        }
    };

    struct {
        Font text;
        Font title;
    } fonts;

    static inline UINT_PTR idGlobalRefreshTimer;
    static constexpr USHORT WM_GlobalRefresh = WM_APP + 0x1234; // choose message that doesn't clash with others in application 

    static void CALLBACK GuiChangesCoalescingTimer (HWND hWnd, UINT, UINT_PTR id, DWORD) {
        idGlobalRefreshTimer = 0;
        KillTimer (hWnd, id);

        // we can refresh DPI-independent and window-independent resources only once here

        EnumThreadWindows (GetCurrentThreadId (),
                           [] (HWND hWnd, LPARAM)->BOOL {
                               return PostMessage (hWnd, WM_GlobalRefresh, 0, 0);
                           }, 0);
    }

public:
    static LPCTSTR Initialize (HINSTANCE hInstance) {
        WNDCLASSEX wndclass = {
            sizeof (WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
            Procedure, 0, 0, hInstance,  NULL,
            NULL, NULL, NULL, L"EXAMPLE", NULL
        };
        return (LPCTSTR) (std::intptr_t) RegisterClassEx (&wndclass);
    }

private:
    LRESULT Dispatch (UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_NCCREATE:
                if (ptrEnableNonClientDpiScaling) {
                    ptrEnableNonClientDpiScaling (hWnd); // required for v1 per-monitor scaling
                }
                this->RefreshVisualMetrics ();
                break;
            case WM_CREATE:
                try {
                    return this->OnCreate (reinterpret_cast <const CREATESTRUCT *> (lParam));
                } catch (...) {
                    return -1;
                }
            case WM_DESTROY:
                return this->OnDestroy ();
            case WM_NCDESTROY:
                delete this; // imagine meme of Mark Zuckerberg pointing a gun at you
                return 0;
            case WM_ENDSESSION:
                if (wParam) {
                    DestroyWindow (hWnd);
                }
                break;

            case WM_GETICON:
                if (lParam && (lParam != this->dpi)) {

                    // OS (taskbars on different displays) or other app asked for icon in different DPI
                    if (auto [found, data] = this->icons.find_in_dpi_cache (wParam, lParam); found) {
                        return (LRESULT) data->icon;

                    } else {
                        auto ndpi = (long) lParam;
                        auto size = GetIconMetrics (this->MapIconSize (wParam));
                        auto icon = LoadBestIcon (reinterpret_cast <HINSTANCE> (&__ImageBase), MAKEINTRESOURCE (1),
                                                  { ndpi * size.cx / 96, ndpi * size.cy / 96 });
                        if (data) {
                            data->type = wParam;
                            data->dpi = ndpi;
                            data->icon = icon;
                        }
                        return (LRESULT) icon;
                    }
                } else {
                    switch (wParam) {
                        case ICON_SMALL2:
                            return (LRESULT) this->icons.standard [this->MapIconSize (wParam)];
                    }
                }
                break;

            case WM_DPICHANGED:
                return this->OnDpiChange (wParam, reinterpret_cast <const RECT *> (lParam));
            case WM_WINDOWPOSCHANGED:
                return this->OnPositionChange (*reinterpret_cast <const WINDOWPOS *> (lParam));

            case WM_THEMECHANGED:
            case WM_SETTINGCHANGE:
            case WM_DWMCOMPOSITIONCHANGED:
                return this->OnPresentationChangeNotification ();

            case WM_GlobalRefresh:
                this->OnVisualEnvironmentChange ();
                InvalidateRect (hWnd, NULL, TRUE);
                break;

            case WM_MOUSEMOVE:
                SetCursor (this->cursor);
                break;

            // painting correctly is a lot more complicated, but this will suffice here

            case WM_CTLCOLORSTATIC:
                SetBkColor ((HDC) wParam, GetSysColor (COLOR_WINDOW));
                SetTextColor ((HDC) wParam, GetSysColor (COLOR_WINDOWTEXT));
            case WM_CTLCOLORBTN:
                return (LRESULT) GetSysColorBrush (COLOR_WINDOW);

            case WM_PRINTCLIENT:
            case WM_ERASEBKGND:
                RECT client;
                if (GetClientRect (hWnd, &client)) {
                    FillRect ((HDC) wParam, &client, GetSysColorBrush (COLOR_WINDOW));
                    return true;
                } else
                    return false;
        }
        return DefWindowProc (hWnd, message, wParam, lParam);
    }

    LRESULT OnCreate (const CREATESTRUCT * cs) {
        CreateWindow (L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_LEFT, 0,0,0,0, hWnd, (HMENU) 100, cs->hInstance, NULL);
        CreateWindow (L"STATIC", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | SS_CENTER, 0,0,0,0, hWnd, (HMENU) 101, cs->hInstance, NULL);
        CreateWindow (L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_CENTER, 0,0,0,0, hWnd, (HMENU) 102, cs->hInstance, NULL);
        CreateWindow (L"BUTTON", L"BUTTON", WS_VISIBLE | WS_CHILD | WS_TABSTOP, 0,0,0,0, hWnd, (HMENU) IDOK, cs->hInstance, NULL);
        this->dpi = GetDPI (this->hWnd);
        this->OnVisualEnvironmentChange ();
        return 0;
    }
    LRESULT OnDestroy () {
        for (auto icon : this->icons.standard) {
            DestroyIcon (icon);
        }
        PostQuitMessage (0);
        return 0;
    }
    LRESULT OnDpiChange (WPARAM dpi, const RECT * r) {
        dpi = LOWORD (dpi);
        if (this->dpi != dpi) {
            // percentual anchors and such are recomputed here
            this->dpi = long (dpi);
        }

        this->OnVisualEnvironmentChange ();
        SetWindowPos (hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, 0);
        return 0;
    }
    LRESULT OnPresentationChangeNotification () {
        idGlobalRefreshTimer = SetTimer (NULL, idGlobalRefreshTimer, 500, GuiChangesCoalescingTimer);
        return 0;
    }
    LRESULT OnVisualEnvironmentChange () {
        auto dpiSystem = GetDPI (NULL);
        auto hTheme = OpenThemeData (hWnd, L"TEXTSTYLE");

        // theme-dependent stuff gets reloaded here
        //  - note that hTheme can be NULL when XP,Vista,7 is in classic mode
        //    or when compatibility mode is imposed onto the window

        LOGFONT lf;
        if (GetThemeSysFont (hTheme, TMT_MSGBOXFONT, &lf) == S_OK) {
            lf.lfHeight = MulDiv (lf.lfHeight, this->dpi, dpiSystem);
            TextScale.Apply (lf);
            this->fonts.text.update (lf);
        } else {
            if (GetObject (GetStockObject (DEFAULT_GUI_FONT), sizeof lf, &lf)) {
                lf.lfHeight = MulDiv (lf.lfHeight, this->dpi, dpiSystem);
                TextScale.Apply (lf);
                this->fonts.text.update (lf);
            }
        }
        if (GetThemeFont (hTheme, NULL, TEXT_MAININSTRUCTION, 0, TMT_FONT, &lf) == S_OK) {
            if (!AreDpiApisScaled (this->hWnd)) {
                lf.lfHeight = MulDiv (lf.lfHeight, this->dpi, dpiSystem);
            }
            TextScale.Apply (lf);
            this->fonts.title.update (lf);
        } else {
            // themes off or unavailable, reuse above one and make it bold
            lf.lfWeight = FW_BOLD;
            lf.lfHeight = MulDiv (lf.lfHeight, this->dpi, dpiSystem);
            TextScale.Apply (lf);
            this->fonts.title.update (lf);
        }

        if (hTheme) {
            CloseThemeData (hTheme);
        }

        // display text size

        wchar_t text [64];
        swprintf (text, 64, L"%ld px TITLE", this->fonts.title.height);
        SetDlgItemText (hWnd, 100, text);

        swprintf (text, 64, L"%ld px text characters test: \x158\xB3 \x338 \x2211 \xBEB\xA675:", this->fonts.text.height);
        SetDlgItemText (hWnd, 101, text);

        swprintf (text, 64, L"Text scale factor: %lu", TextScale.current);
        SetDlgItemText (hWnd, 102, text);

        // set the new font(s) to appropriate children

        SendDlgItemMessage (hWnd, 100, WM_SETFONT, (WPARAM) this->fonts.title.handle, 1);
        SendDlgItemMessage (hWnd, 101, WM_SETFONT, (WPARAM) this->fonts.text.handle, 1);
        SendDlgItemMessage (hWnd, 102, WM_SETFONT, (WPARAM) this->fonts.text.handle, 1);
        SendDlgItemMessage (hWnd, IDOK, WM_SETFONT, (WPARAM) this->fonts.text.handle, 1);

        // refresh everthing else

        this->cursor = LoadCursor (NULL, IDC_ARROW);
        this->RefreshVisualMetrics (dpiSystem);

        // DPI changes also size of window icons

        for (auto i = 0u; i != IconSizesCount; ++i) {
            auto m = GetIconMetrics ((IconSize) i, dpiSystem);
            if (auto icon = LoadBestIcon (reinterpret_cast <HINSTANCE> (&__ImageBase), MAKEINTRESOURCE (1),
                                          GetIconMetrics ((IconSize) i, dpiSystem))) {
                if (this->icons.standard [i]) {
                    DestroyIcon (this->icons.standard [i]);
                }
                this->icons.standard [i] = icon;
            }
        }

        // drop DPI-specific icon cache

        for (auto & item : this->icons.dpi_cache) {
            if (item.icon) {
                DestroyIcon (item.icon);

                item.type = 0;
                item.dpi = 0;
                item.icon = NULL;
            }
        }

        // set primary pair of icons for the window

        SendMessage (hWnd, WM_SETICON, ICON_SMALL, (LPARAM) this->icons.standard [MapIconSize (ICON_SMALL)]);
        SendMessage (hWnd, WM_SETICON, ICON_BIG, (LPARAM) this->icons.standard [MapIconSize (ICON_BIG)]);

        return 0;
    }

    void DeferChildPos (HDWP & hDwp, UINT id, const POINT & position, const SIZE & size, UINT flags = 0) {
        hDwp = DeferWindowPos (hDwp, GetDlgItem (this->hWnd, id), NULL,
                               position.x, position.y, size.cx, size.cy,
                               SWP_NOACTIVATE | SWP_NOZORDER | flags);
    }

    LRESULT OnPositionChange (const WINDOWPOS & position) {
        if (!(position.flags & SWP_NOSIZE) || (position.flags & (SWP_SHOWWINDOW | SWP_FRAMECHANGED))) {

            RECT client;
            if (GetClientRect (hWnd, &client)) {
                if (HDWP hDwp = BeginDeferWindowPos (2)) {
                    POINT center = { client.right / 2, client.bottom / 2 };

                    // use a little larger than recommended size from uxguide: https://docs.microsoft.com/en-us/windows/win32/uxguide/ctrl-command-buttons
                    SIZE sizeButton = {
                        (85 * this->dpi * TextScale.current) / (96 * 100),
                        (25 * this->dpi * TextScale.current) / (96 * 100)
                    };
                    // center it
                    POINT posButton = {
                        center.x - sizeButton.cx / 2,
                        center.y - sizeButton.cy / 2,
                    };

                    DeferChildPos (hDwp, IDOK, posButton, sizeButton);

                    // make the label height fit the font tightly + the border
                    SIZE sizeLabel = {
                        client.right,
                        this->fonts.text.height + 2 * this->metrics [SM_CYBORDER]
                    };
                    POINT posLabel = {
                        0,
                        posButton.y - sizeLabel.cy - (4 * this->dpi / 96) // uxguide says 4px spacing
                    };
                    DeferChildPos (hDwp, 101, posLabel, sizeLabel);

                    SIZE sizeLabel2 = {
                        client.right,
                        this->fonts.text.height + 2 * this->metrics [SM_CYBORDER]
                    };
                    POINT posLabel2 = {
                        0,
                        posButton.y + sizeButton.cy + (4 * this->dpi / 96)
                    };
                    DeferChildPos (hDwp, 102, posLabel2, sizeLabel2);

                    // title
                    SIZE sizeTitle = {
                        client.right / 3,
                        this->fonts.title.height
                    };
                    POINT posTitle = {
                        client.right / 3,
                        posLabel.y - sizeTitle.cy - (7 * this->dpi / 96)
                    };
                    DeferChildPos (hDwp, 100, posTitle, sizeTitle);

                    EndDeferWindowPos (hDwp);
                }
            }
        }
        return 0;
    }

    // Procedure
    //  - initialization and forwarding to actual procedure (member function)
    //  - if we want to avoid the 'if' we can split this into two procedures and SetWindowLongPtr (..., GWLP_WNDPROC, ...)
    //  - we are also eating exceptions so they don't escape to foreign frames
    //
    static LRESULT CALLBACK Procedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        try {
            if (auto window = GetWindowLongPtr (hWnd, GWLP_USERDATA)) {
                return reinterpret_cast <Window *> (window)->Dispatch (message, wParam, lParam);

            } else {
                switch (message) {
                    case WM_NCCREATE:
                        if (auto window = new (std::nothrow) Window (hWnd)) {
                            SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG_PTR) window);
                            return window->Dispatch (WM_NCCREATE, wParam, lParam);
                        } else
                            return FALSE;

                    case WM_DESTROY:
                        PostQuitMessage (0);
                        break;
                }
                return DefWindowProc (hWnd, message, wParam, lParam);
            }
        } catch (...) {
            DestroyWindow (hWnd);
            return 0;
        }
    }
};

int CALLBACK wWinMain (_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow) {
    InitCommonControls ();
    
    if (HMODULE hUser32 = GetModuleHandle (L"USER32")) {
        Symbol (hUser32, ptrEnableNonClientDpiScaling, "EnableNonClientDpiScaling");
        Symbol (hUser32, pfnGetDpiForSystem, "GetDpiForSystem");
        Symbol (hUser32, pfnGetDpiForWindow, "GetDpiForWindow");
        Symbol (hUser32, ptrGetSystemMetricsForDpi, "GetSystemMetricsForDpi");
        Symbol (hUser32, ptrGetWindowDpiAwarenessContext, "GetWindowDpiAwarenessContext");
        Symbol (hUser32, ptrAreDpiAwarenessContextsEqual, "AreDpiAwarenessContextsEqual");
    }
    if (HMODULE hComCtl32 = GetModuleHandle (L"COMCTL32")) {
        Symbol (hComCtl32, ptrLoadIconWithScaleDown, "LoadIconWithScaleDown");
    }

    if (auto atom = Window::Initialize (hInstance)) {
        static const auto D = CW_USEDEFAULT;
        if (auto hWnd = CreateWindow (atom, L"Win32 DPI-aware window example",
                                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                      D, D, D, D, HWND_DESKTOP, NULL, hInstance, NULL)) {

            if (TextScale.Initialize ()) {
                HANDLE hThreadPoolWait = NULL;
                RegisterWaitForSingleObject (&hThreadPoolWait, TextScale.hEvent,
                                             [] (PVOID hWnd, BOOLEAN) {
                                                 if (TextScale.OnEvent ()) {
                                                     SendMessage ((HWND) hWnd, WM_SETTINGCHANGE, 0, 0);
                                                 }
                                             }, hWnd, INFINITE, 0);
            }
                
            ShowWindow (hWnd, nCmdShow);

            MSG message;
            message.wParam = 0;

            while (GetMessage (&message, NULL, 0u, 0u)) {
                if (!IsDialogMessage (GetAncestor (message.hwnd, GA_ROOT), &message)) {
                    TranslateMessage (&message);
                    DispatchMessage (&message);
                }
            }
            
            return (int) message.wParam;
        }
    }
    return (int) GetLastError ();
}
