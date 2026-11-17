// Loads the real plugin DLL, hosts it in a hidden window and drives it the way
// Total Commander does: load, paint, select, copy, zoom, load next, close.
// Usage: plugin_smoke <tcl-mupdfv.wlx64> <samples dir>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(cond, what)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, what);         \
            failures++;                                                \
        }                                                              \
    } while (0)

using LoadW = HWND(__stdcall*)(HWND, WCHAR*, int);
using LoadNextW = int(__stdcall*)(HWND, HWND, WCHAR*, int);
using Close = void(__stdcall*)(HWND);
using SendCommand = int(__stdcall*)(HWND, int, int);
using Detect = void(__stdcall*)(char*, int);

const int kW = 800, kH = 900;

struct Capture {
    std::vector<unsigned char> px;  // BGRA, top-down
    int w = 0, h = 0;
    COLORREF at(int x, int y) const {
        const unsigned char* p = &px[(static_cast<size_t>(y) * w + x) * 4];
        return RGB(p[2], p[1], p[0]);
    }
    int count(COLORREF c, int tolerance = 6) const {
        int n = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                COLORREF v = at(x, y);
                if (abs(GetRValue(v) - GetRValue(c)) <= tolerance && abs(GetGValue(v) - GetGValue(c)) <= tolerance &&
                    abs(GetBValue(v) - GetBValue(c)) <= tolerance)
                    n++;
            }
        return n;
    }
    // Background pixels on row y.
    int grayRun(int y) const {
        int n = 0;
        for (int x = 0; x < w; x++) n += at(x, y) == RGB(128, 128, 128);
        return n;
    }
    // White (page) pixels on row y.
    int whiteRun(int y) const {
        int n = 0;
        for (int x = 0; x < w; x++) n += at(x, y) == RGB(255, 255, 255);
        return n;
    }
};

Capture capture(HWND hwnd) {
    Capture c;
    RECT rc;
    GetClientRect(hwnd, &rc);
    c.w = rc.right;
    c.h = rc.bottom;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = c.w;
    bi.bmiHeader.biHeight = -c.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, bmp);
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem), PRF_CLIENT);
    // Warnings found while painting resize the view; paint once more.
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem), PRF_CLIENT);
    GdiFlush();
    c.px.assign(static_cast<unsigned char*>(bits), static_cast<unsigned char*>(bits) + c.w * c.h * 4);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(nullptr, screen);
    return c;
}

// With TCL_SMOKE_DUMP=<dir>, captures are also written there as BMP files.
void dump(const Capture& c, const char* name) {
    char dir[MAX_PATH];
    if (!GetEnvironmentVariableA("TCL_SMOKE_DUMP", dir, MAX_PATH)) return;
    std::string path = std::string(dir) + "\\" + name + ".bmp";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    BITMAPINFOHEADER ih{sizeof ih, c.w, -c.h, 1, 32, BI_RGB};
    DWORD bytes = static_cast<DWORD>(c.px.size());
    BITMAPFILEHEADER fh{0x4D42, static_cast<DWORD>(sizeof fh + sizeof ih + bytes), 0, 0, sizeof fh + sizeof ih};
    std::fwrite(&fh, sizeof fh, 1, f);
    std::fwrite(&ih, sizeof ih, 1, f);
    std::fwrite(c.px.data(), 1, bytes, f);
    std::fclose(f);
}

std::wstring clipboardText() {
    std::wstring s;
    if (!OpenClipboard(nullptr)) return s;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            s = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return s;
}

void clearClipboard() {
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        CloseClipboard();
    }
}

void pump() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

const COLORREF kBar = RGB(255, 244, 206);

// The plugin reads Alt with GetKeyState, which follows this thread's key state.
void setAlt(bool down) {
    BYTE keys[256];
    GetKeyboardState(keys);
    keys[VK_MENU] = down ? 0x80 : 0;
    SetKeyboardState(keys);
}

void click(HWND w, int x, int y, bool dbl = false) {
    SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    if (dbl) {
        SendMessageW(w, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    }
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::printf("usage: plugin_smoke <plugin dll> <samples dir>\n");
        return 2;
    }
    std::wstring dir = argv[2];
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        std::printf("cannot load %ls (error %lu)\n", argv[1], GetLastError());
        return 2;
    }
    auto load = reinterpret_cast<LoadW>(GetProcAddress(dll, "ListLoadW"));
    auto loadNext = reinterpret_cast<LoadNextW>(GetProcAddress(dll, "ListLoadNextW"));
    auto close = reinterpret_cast<Close>(GetProcAddress(dll, "ListCloseWindow"));
    auto send = reinterpret_cast<SendCommand>(GetProcAddress(dll, "ListSendCommand"));
    auto detect = reinterpret_cast<Detect>(GetProcAddress(dll, "ListGetDetectString"));
    CHECK(load && loadNext && close && send && detect, "missing export");
    CHECK(GetProcAddress(dll, "ListLoad") && GetProcAddress(dll, "ListLoadNext"), "missing ANSI export");
    if (!load || !loadNext || !close || !send || !detect) return 1;

    char det[64] = {};
    detect(det, sizeof det);
    CHECK(std::string(det) == "EXT=\"PDF\"", "detect string");

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TLister";
    RegisterClassW(&wc);
    HWND parent = CreateWindowExW(0, L"TLister", L"host", WS_POPUP, 0, 0, kW, kH, nullptr, nullptr, wc.hInstance, nullptr);
    if (!parent) {
        std::printf("cannot create the host window (error %lu); is a desktop available?\n", GetLastError());
        return 2;
    }

    std::wstring bad = dir + L"\\does-not-exist.pdf";
    CHECK(load(parent, bad.data(), 0) == nullptr, "missing file must be refused");

    std::wstring text = dir + L"\\text.pdf";
    HWND win = load(parent, text.data(), 0);
    CHECK(win != nullptr, "ListLoadW(text.pdf)");
    if (!win) return 1;
    pump();

    // Fit page: one whole page visible (background above and below it), blue
    // rectangle drawn, no warning bar.
    Capture c = capture(win);
    dump(c, "1-text");
    int row = c.h / 3;
    int width1 = c.whiteRun(row);
    CHECK(width1 > c.w / 2 && width1 < c.w * 95 / 100, "page narrower than the view");
    CHECK(c.at(c.w / 2, 2) == RGB(128, 128, 128) && c.at(c.w / 2, c.h - 2) == RGB(128, 128, 128),
          "whole page height visible");
    CHECK(c.count(RGB(0, 0, 255)) > 1000, "blue rectangle rendered");
    CHECK(c.count(RGB(0, 0, 0), 60) > 200, "text rendered");
    CHECK(c.at(2, 2) != kBar, "no warning bar for text.pdf");

    // Select all + copy.
    clearClipboard();
    CHECK(send(win, 3 /*lc_selectall*/, 0) == 0, "lc_selectall");
    CHECK(send(win, 1 /*lc_copy*/, 0) == 0, "lc_copy");
    std::wstring all = clipboardText();
    CHECK(all.find(L"Hello Helvetica") != std::wstring::npos, "copy all: page 1 text");
    CHECK(all.find(L"Second page text") != std::wstring::npos, "copy all: page 2 text");
    CHECK(all.find(L"\x03b1\x03b2\x03b3") != std::wstring::npos, "copy all: Symbol text");
    Capture sel = capture(win);
    dump(sel, "2-select-all");
    CHECK(sel.count(RGB(150, 195, 255), 10) > 500, "selection highlight drawn");

    // Alt+drag over the top part of page 1 only, then Ctrl+C.
    setAlt(true);
    SendMessageW(win, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    SendMessageW(win, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(kW - 30, c.h / 3));
    SendMessageW(win, WM_LBUTTONUP, 0, MAKELPARAM(kW - 30, c.h / 3));
    setAlt(false);
    clearClipboard();
    SendMessageW(win, WM_KEYDOWN, 'C', 0);  // without Ctrl: nothing
    CHECK(clipboardText().empty(), "plain C does not copy");
    send(win, 1, 0);
    std::wstring part = clipboardText();
    CHECK(part.find(L"Hello Helvetica") != std::wstring::npos, "drag copy contains first line");
    CHECK(part.find(L"Second page text") == std::wstring::npos, "drag copy stops on page 1");

    // Click clears the selection.
    click(win, 100, 100);
    clearClipboard();
    send(win, 1, 0);
    CHECK(clipboardText().empty(), "click clears the selection");

    // Plain drag pans (and selects nothing).
    int top = GetScrollPos(win, SB_VERT);
    SendMessageW(win, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(400, 600));
    SendMessageW(win, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(400, 300));
    SendMessageW(win, WM_LBUTTONUP, 0, MAKELPARAM(400, 300));
    CHECK(GetScrollPos(win, SB_VERT) == top + 300, "drag pans the view");
    send(win, 1, 0);
    CHECK(clipboardText().empty(), "panning selects nothing");
    SendMessageW(win, WM_KEYDOWN, VK_HOME, 0);

    // Double-click selects the word under the cursor; on empty space, nothing.
    // "Helvetica" in "Hello Helvetica" is at about x 175-260, y 87 at this size.
    click(win, 215, 87, true);
    send(win, 1, 0);
    std::wstring word = clipboardText();
    std::printf("double-click copied \"%ls\"\n", word.c_str());
    CHECK(word.find(L"Helvetica") == 0 && word.size() <= 10, "double-click selects one word");
    clearClipboard();
    click(win, 400, 600, true);
    send(win, 1, 0);
    std::wstring empty = clipboardText();
    std::printf("empty-space double-click copied \"%ls\"\n", empty.c_str());
    CHECK(empty.empty(), "double-click on empty space selects nothing");

    // Ctrl+wheel zooms in around the cursor.
    POINT pt{kW / 2, kH / 2};
    ClientToScreen(win, &pt);
    SendMessageW(win, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, 2 * WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
    Capture z = capture(win);
    dump(z, "3-zoom-in");
    CHECK(z.grayRun(z.h / 2) == 0, "zoomed page wider than the view");
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE};
    CHECK(GetScrollInfo(win, SB_HORZ, &si) && si.nMax + 1 > static_cast<int>(si.nPage), "horizontal scroll bar after zoom in");
    SendMessageW(win, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, static_cast<WORD>(-6 * WHEEL_DELTA)), MAKELPARAM(pt.x, pt.y));
    Capture zo = capture(win);
    CHECK(zo.whiteRun(zo.h / 3) < width1 * 8 / 10, "zoom out makes the page narrower");

    // Plain wheel scrolls.
    int before = GetScrollPos(win, SB_VERT);
    SendMessageW(win, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(pt.x, pt.y));
    CHECK(GetScrollPos(win, SB_VERT) > before, "wheel scrolls down");

    // Warnings: JPEG 2000, missing annotation appearance, missing CJK CMap.
    struct { const wchar_t* file; const wchar_t* what; } warn[] = {
        {L"jpx.pdf", L"JPEG 2000"}, {L"annot.pdf", L"annotation"}, {L"cjk.pdf", L"CJK"}};
    for (auto& w : warn) {
        std::wstring f = dir + L"\\" + w.file;
        CHECK(loadNext(parent, win, f.data(), 0) == 0, "ListLoadNextW");
        Capture b = capture(win);
        dump(b, (std::string("4-") + static_cast<char>(w.file[0])).c_str());
        std::printf("%ls: bar pixel %06lx\n", w.file, static_cast<unsigned long>(b.at(2, 2)));
        CHECK(b.at(2, 2) == kBar, "warning bar shown");
        // Clicking the bar hides it.
        SendMessageW(win, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 3));
        SendMessageW(win, WM_LBUTTONUP, 0, MAKELPARAM(10, 3));
        Capture h = capture(win);
        CHECK(h.at(2, 2) != kBar, "warning bar hidden after click");
    }
    CHECK(loadNext(parent, win, bad.data(), 0) != 0, "ListLoadNextW refuses a missing file");

    close(win);
    CHECK(!IsWindow(win), "ListCloseWindow destroys the window");

    // remember_zoom in <plugin>.ini: off by default, on keeps the last Ctrl+wheel zoom.
    std::wstring ini = argv[1];
    ini = ini.substr(0, ini.find_last_of(L'.')) + L".ini";
    auto zoomedLoad = [&](bool remember, bool zoomIn) {
        WritePrivateProfileStringW(L"General", L"remember_zoom", remember ? L"1" : L"0", ini.c_str());
        HWND w = load(parent, text.data(), 0);
        if (zoomIn) SendMessageW(w, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, 2 * WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
        int width = capture(w).whiteRun(row);
        close(w);
        return width;
    };
    DeleteFileW(ini.c_str());
    CHECK(zoomedLoad(true, false) == width1, "remember_zoom without a saved zoom fits the page");
    zoomedLoad(true, true);
    CHECK(GetPrivateProfileIntW(L"General", L"zoom", 0, ini.c_str()) > 100, "zoom saved to the INI");
    CHECK(zoomedLoad(true, false) > width1, "remembered zoom applied to the next file");
    CHECK(zoomedLoad(false, false) == width1, "remember_zoom=0 fits the page");
    DeleteFileW(ini.c_str());
    DestroyWindow(parent);
    FreeLibrary(dll);
    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
