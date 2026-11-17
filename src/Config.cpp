#include "Config.h"

#include <windows.h>

#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mupdfv {

namespace {
// <plugin path without extension>.ini
std::wstring iniPath() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), buf, MAX_PATH * 4);
    std::wstring p(buf, n);
    size_t dot = p.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > p.find_last_of(L"\\/")) p.erase(dot);
    return p + L".ini";
}
}  // namespace

Config Config::load() {
    std::wstring ini = iniPath();
    Config c;
    c.rememberZoom = GetPrivateProfileIntW(L"General", L"remember_zoom", 0, ini.c_str()) != 0;
    c.zoomPercent = static_cast<int>(GetPrivateProfileIntW(L"General", L"zoom", 0, ini.c_str()));
    if (c.zoomPercent < 0) c.zoomPercent = 0;
    return c;
}

void Config::saveZoom(int percent) {
    std::wstring value = percent > 0 ? std::to_wstring(percent) : L"0";
    WritePrivateProfileStringW(L"General", L"zoom", value.c_str(), iniPath().c_str());
}

}  // namespace mupdfv
