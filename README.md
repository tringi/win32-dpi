# Win32 DPI-aware window example

Trivial example on how to write Win32 DPI-aware GUI application that scales properly on everything starting Windows XP and ending with latest Windows 11 (tested on insider build 10.0.22523.1).

## In a nutshell

* Primary monitor is usually at System DPI (unless changed without restart)
* Other monitors can be at higher or lower DPI than System
* All metrics normally (pre-v2) reported to the application are in System DPI
* Some APIs (GetThemeFont) scale the reported values when in PerMonitorV2 awareness mode
* It's error-prone to call GetSystemMetrics wherever needed and scale manually, precompute these
* Mostly everything derives metrics from font size
   * recreate fonts on DPI change, remember height, rescale accordingly
* Window and Taskbar icons sizes change too
   * the OS may ask for different DPI icon (e.g. for Taskbar on monitor with different DPI)
* Since Windows 10 there is mismatch between reality and documented Taskbar icon size
   * Taskbar used to use [ICON_BIG](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-geticon)/[SM_CXICON](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmetrics)-sized icon (32×32)
     but starting with Windows 10, it's resized down to 24×24 (so called *Start* size on XP)
   * there is no API to query, so testing OS version, and updating code as Microsoft makes changes, remains
   * Alt+Tab in Windows 11 now does the same, takes *BIG* icon, and scales it down to size of a *SMALL* one (16×16)

## Additional

* The example also shows how to track "TextScaleFactor" for UWP apps, see Settings > Accessibility > Text size,
  and apply it to Win32 window content scaling.
* Multiple WM_SETTINGCHANGE and other GUI change notifications can come in quick succession, so it is possible to alleviate
  excess refresh and flickering by coalescing those, to improve user experience.

## Manifest

For the application to support DPI scaling to the full extent of what the underlying Operating System supports, the process DPI awareness must be set.
That's accomplished either through manifest, or calling API(s). The API way is complicated. We will use my **[rsrcgen.exe](https://github.com/tringi/rsrcgen)**
tool to generate manifest that will request everything known so far, and then deal with what we get at runtime.

### API alternative:
* [SetProcessDPIAware](https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setprocessdpiaware) - Windows Vista / Server 2008
* [SetProcessDpiAwareness](https://docs.microsoft.com/cs-cz/windows/win32/api/shellscalingapi/nf-shellscalingapi-setprocessdpiawareness) - Windows 8.1 / Server 2012 R2
* [SetProcessDpiAwarenessContext](https://docs.microsoft.com/cs-cz/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext) - Windows 10 1607 / LTSB 2016 / Server 2016
   - with [DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2](https://docs.microsoft.com/cs-cz/windows/win32/hidpi/dpi-awareness-context) - Windows 10 1703
   - with [DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED](https://docs.microsoft.com/cs-cz/windows/win32/hidpi/dpi-awareness-context) - Windows 10 1809 / LTSC 2019 / Server 2019

## Additional reading

* [High DPI Desktop Application Development on Windows](https://docs.microsoft.com/cs-cz/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows?redirectedfrom=MSDN)
* [Mixed-Mode DPI Scaling and DPI-aware APIs](https://docs.microsoft.com/cs-cz/windows/win32/hidpi/high-dpi-improvements-for-desktop-applications)
* [Setting the default DPI awareness for a process](https://docs.microsoft.com/cs-cz/previous-versions/windows/desktop/legacy/mt846517)
* [Improving the high-DPI experience in GDI based Desktop Apps](https://blogs.windows.com/windowsdeveloper/2017/05/19/improving-high-dpi-experience-gdi-based-desktop-apps/#VllxTW8vOXIB4HqW.97)
* [High-DPI Scaling Improvements for Desktop Applications in the Windows 10 Creators Update (1703)](https://blogs.windows.com/windowsdeveloper/2017/04/04/high-dpi-scaling-improvements-desktop-applications-windows-10-creators-update/#Due3kFlj32WEmiwf.97)
* [High DPI Scaling Improvements for Desktop Applications and “Mixed Mode” DPI Scaling in the Windows 10 Anniversary Update (1607)](https://blogs.windows.com/windowsdeveloper/2016/10/24/high-dpi-scaling-improvements-for-desktop-applications-and-mixed-mode-dpi-scaling-in-the-windows-10-anniversary-update/#2rElZ4ZhV2dvcUOp.97)

# ISC License

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
