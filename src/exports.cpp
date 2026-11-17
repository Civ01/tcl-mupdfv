// Total Commander Lister plugin entry points. No exception may cross this ABI.
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "Viewer.h"
#include "listplug.h"

using mupdfv::Viewer;

namespace {
std::wstring fromAnsi(const char* s) {
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}
}  // namespace

BOOL APIENTRY DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
    // On FreeLibrary (not at process exit): unmap the Windows font files and
    // drop the window class, so a reloaded DLL registers its own procedure.
    if (reason == DLL_PROCESS_DETACH && !reserved) {
        tcl_fonts_cleanup();
        Viewer::unregisterClass();
    }
    return TRUE;
}

extern "C" {

void __stdcall ListGetDetectString(char* detect, int maxlen) {
    static const char kDetect[] = "EXT=\"PDF\"";
    if (!detect || maxlen <= 0) return;
    size_t n = std::min(sizeof kDetect - 1, static_cast<size_t>(maxlen) - 1);
    memcpy(detect, kDetect, n);
    detect[n] = '\0';
}

HWND __stdcall ListLoadW(HWND parent, WCHAR* file, int) {
    if (!file) return nullptr;
    try {
        Viewer* v = Viewer::create(parent, file);
        return v ? v->hwnd() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

HWND __stdcall ListLoad(HWND parent, char* file, int flags) {
    if (!file) return nullptr;
    try {
        std::wstring w = fromAnsi(file);
        return ListLoadW(parent, w.data(), flags);
    } catch (...) {
        return nullptr;
    }
}

int __stdcall ListLoadNextW(HWND, HWND listWin, WCHAR* file, int) {
    if (!file) return LISTPLUGIN_ERROR;
    try {
        Viewer* v = Viewer::from(listWin);
        return v && v->load(file) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
    } catch (...) {
        return LISTPLUGIN_ERROR;
    }
}

int __stdcall ListLoadNext(HWND parent, HWND listWin, char* file, int flags) {
    if (!file) return LISTPLUGIN_ERROR;
    try {
        std::wstring w = fromAnsi(file);
        return ListLoadNextW(parent, listWin, w.data(), flags);
    } catch (...) {
        return LISTPLUGIN_ERROR;
    }
}

void __stdcall ListCloseWindow(HWND listWin) {
    if (listWin && IsWindow(listWin)) DestroyWindow(listWin);
}

int __stdcall ListSendCommand(HWND listWin, int command, int) {
    try {
        Viewer* v = Viewer::from(listWin);
        if (!v) return LISTPLUGIN_ERROR;
        if (command == lc_copy) v->copy();
        else if (command == lc_selectall) v->selectAll();
        else return LISTPLUGIN_ERROR;
        return LISTPLUGIN_OK;
    } catch (...) {
        return LISTPLUGIN_ERROR;
    }
}

}  // extern "C"
