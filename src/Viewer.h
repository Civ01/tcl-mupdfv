// The Lister window: pages stacked vertically, fit to width on open.
// Mouse wheel scrolls, Ctrl+wheel zooms, dragging selects text, Ctrl+C copies.
#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Document.h"

namespace mupdfv {

class Viewer {
public:
    // Creates the window and loads `file`. Returns nullptr if the file can't be shown.
    static Viewer* create(HWND parent, const std::wstring& file);
    static Viewer* from(HWND hwnd);
    // Call on DLL unload, after all viewer windows are gone.
    static void unregisterClass();
    HWND hwnd() const { return hwnd_; }

    bool load(const std::wstring& file);
    void copy();
    void selectAll();

private:
    struct Slot { int y = 0, w = 0, h = 0; };  // page position at the current zoom, in pixels
    struct Pos { int page = 0; fz_point pt{}; };  // a point on a page, in page space
    struct Tile {
        int page = -1;
        float zoom = 0;
        RECT area{};  // in page pixels at the zoom it was rendered with
        HBITMAP bmp = nullptr;
        unsigned use = 0;
    };

    explicit Viewer(HWND hwnd);
    ~Viewer();
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void layout();
    void updateScrollBars();
    void scrollTo(int x, int y);
    void zoomAt(POINT client, int steps);
    void paint(HDC dc);
    const Tile* tileFor(int page, const RECT& need);
    void freeTiles();
    void drawSelection(HDC dc, int page, int px, int py);
    void updateBar();

    int pageX(int page) const;
    int viewHeight() const;
    bool pageAt(POINT client, Pos& out, bool clamp) const;
    fz_point topLeft(int page) const;
    fz_point bottomRight(int page) const;
    // Selection range on `page` (false if the page is outside the selection).
    bool selectionOn(int page, fz_point& a, fz_point& b) const;
    void selectionChanged();
    void dragTo(POINT client);

    HWND hwnd_;
    std::unique_ptr<Document> doc_;
    std::vector<Slot> slots_;
    float zoom_ = 1.0f;  // pixels per point
    bool fit_ = true;
    int docW_ = 0, docH_ = 0, scrollX_ = 0, scrollY_ = 0;
    int gap_ = 8, line_ = 40;
    bool inLayout_ = false;
    int wheel_ = 0, zoomWheel_ = 0, hwheel_ = 0;

    std::vector<Tile> tiles_;
    unsigned tileClock_ = 0;
    HBITMAP back_ = nullptr;
    SIZE backSize_{};

    bool hasSel_ = false, dragging_ = false;
    Pos anchor_, focus_;
    POINT dragPoint_{};
    std::unordered_map<int, std::vector<fz_quad>> quads_;

    HFONT barFont_ = nullptr;
    int barH_ = 0;
    unsigned barShown_ = 0, barHidden_ = 0;
    std::wstring barText_;
};

}  // namespace mupdfv
