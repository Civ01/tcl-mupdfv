// Settings from tcl-mupdfv.ini next to the plugin DLL. Read on every load,
// so edits apply to the next file opened.
#pragma once

namespace mupdfv {

struct Config {
    bool rememberZoom = false;  // [General] remember_zoom
    int zoomPercent = 0;        // [General] zoom: last zoom in %, 0 = fit page

    static Config load();
    // Stores the zoom (0 = fit page). Only call when rememberZoom is on.
    static void saveZoom(int percent);
};

}  // namespace mupdfv
