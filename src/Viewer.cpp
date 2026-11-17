#include "Viewer.h"

#include "Config.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mupdfv {

namespace {
const wchar_t kClass[] = L"TclMupdfvViewer";
constexpr float kMinZoom = 0.05f, kMaxZoom = 32.0f;  // pixels per point
constexpr float kZoomStep = 1.15f;
constexpr size_t kMaxTiles = 4;
constexpr UINT_PTR kDragTimer = 1;
const COLORREF kBackground = RGB(128, 128, 128);
const COLORREF kSelection = RGB(150, 195, 255);  // drawn with R2_MASKPEN: text stays dark
const COLORREF kBarBack = RGB(255, 244, 206);

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool contains(const RECT& outer, const RECT& inner) {
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
}
}  // namespace

// ------------------------------------------------------------------ lifetime

namespace {
bool g_registered = false;
}

void Viewer::unregisterClass() {
    if (g_registered) UnregisterClassW(kClass, reinterpret_cast<HINSTANCE>(&__ImageBase));
    g_registered = false;
}

Viewer* Viewer::create(HWND parent, const std::wstring& file) {
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(&__ImageBase);
    bool& registered = g_registered;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &Viewer::proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
        registered = true;
    }
    RECT rc;
    GetClientRect(parent, &rc);
    HWND hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS, 0, 0,
                                rc.right, rc.bottom, parent, nullptr, inst, nullptr);
    if (!hwnd) return nullptr;
    Viewer* v = from(hwnd);
    if (!v || !v->load(file)) {
        DestroyWindow(hwnd);
        return nullptr;
    }
    ShowWindow(hwnd, SW_SHOW);
    return v;
}

Viewer* Viewer::from(HWND hwnd) {
    return hwnd ? reinterpret_cast<Viewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) : nullptr;
}

Viewer::Viewer(HWND hwnd) : hwnd_(hwnd) {
    HDC dc = GetDC(hwnd);
    dpi_ = GetDeviceCaps(dc, LOGPIXELSY);
    gap_ = MulDiv(8, dpi_, 96);
    line_ = MulDiv(40, dpi_, 96);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    barFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
    ReleaseDC(hwnd, dc);
}

Viewer::~Viewer() {
    saveZoom();
    freeTiles();
    if (back_) DeleteObject(back_);
    if (barFont_) DeleteObject(barFont_);
}

// 100% = actual size on screen (1 point = 1/72 inch).
float Viewer::percentToZoom(int percent) const {
    return std::clamp(percent / 100.0f * dpi_ / 72.0f, kMinZoom, kMaxZoom);
}

int Viewer::zoomToPercent() const {
    return std::max(1, static_cast<int>(std::lround(zoom_ * 72.0f / dpi_ * 100.0f)));
}

void Viewer::saveZoom() {
    if (doc_ && Config::load().rememberZoom) Config::saveZoom(fit_ ? 0 : zoomToPercent());
}

bool Viewer::load(const std::wstring& file) {
    auto doc = Document::open(toUtf8(file));
    if (!doc) return false;
    saveZoom();  // before the previous file is replaced (ListLoadNext)
    doc_ = std::move(doc);
    freeTiles();
    quads_.clear();
    hasSel_ = false;
    drag_ = Drag::None;
    Config cfg = Config::load();
    fit_ = !(cfg.rememberZoom && cfg.zoomPercent > 0);
    if (!fit_) zoom_ = percentToZoom(cfg.zoomPercent);
    scrollX_ = scrollY_ = 0;
    barShown_ = barHidden_ = 0;
    updateBar();
    layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

// ------------------------------------------------------------------ layout

int Viewer::viewHeight() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    return std::max(0, static_cast<int>(rc.bottom) - barH_);
}

int Viewer::pageX(int page) const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    return (std::max(docW_, static_cast<int>(rc.right)) - slots_[static_cast<size_t>(page)].w) / 2;
}

void Viewer::layout() {
    if (!doc_ || inLayout_) return;
    inLayout_ = true;
    int oldW = docW_, oldH = docH_;
    // Showing or hiding a scroll bar changes the client size; repeat until stable.
    for (int pass = 0; pass < 3; pass++) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int n = doc_->pageCount();
        if (fit_) {
            // Fit one whole page (the largest) into the view.
            float widest = 1, tallest = 1;
            for (int i = 0; i < n; i++) {
                fz_rect b = doc_->bounds(i);
                widest = std::max(widest, b.x1 - b.x0);
                tallest = std::max(tallest, b.y1 - b.y0);
            }
            float byWidth = (rc.right - 2 * gap_) / widest;
            float byHeight = (rc.bottom - barH_ - 2 * gap_) / tallest;
            zoom_ = std::clamp(std::min(byWidth, byHeight), kMinZoom, kMaxZoom);
        }
        slots_.resize(static_cast<size_t>(n));
        int y = gap_;
        docW_ = 0;
        for (int i = 0; i < n; i++) {
            fz_rect b = doc_->bounds(i);
            Slot& s = slots_[static_cast<size_t>(i)];
            s.w = std::max(1, static_cast<int>(std::lround((b.x1 - b.x0) * zoom_)));
            s.h = std::max(1, static_cast<int>(std::lround((b.y1 - b.y0) * zoom_)));
            s.y = y;
            y += s.h + gap_;
            docW_ = std::max(docW_, s.w + 2 * gap_);
        }
        docH_ = y;
        updateScrollBars();
        RECT after;
        GetClientRect(hwnd_, &after);
        if (after.right == rc.right && after.bottom == rc.bottom) break;
    }
    // Keep the same part of the document in view when the zoom changed.
    if (oldH > 0 && oldH != docH_) {
        scrollX_ = static_cast<int>(static_cast<long long>(scrollX_) * docW_ / std::max(1, oldW));
        scrollY_ = static_cast<int>(static_cast<long long>(scrollY_) * docH_ / oldH);
        updateScrollBars();
    }
    inLayout_ = false;
}

void Viewer::updateScrollBars() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int vh = viewHeight();
    scrollX_ = std::clamp(scrollX_, 0, std::max(0, docW_ - static_cast<int>(rc.right)));
    scrollY_ = std::clamp(scrollY_, 0, std::max(0, docH_ - vh));
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMax = docH_ - 1;
    si.nPage = static_cast<UINT>(vh);
    si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    si.nMax = docW_ - 1;
    si.nPage = static_cast<UINT>(rc.right);
    si.nPos = scrollX_;
    SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
}

void Viewer::scrollTo(int x, int y) {
    int ox = scrollX_, oy = scrollY_;
    scrollX_ = x;
    scrollY_ = y;
    updateScrollBars();
    if (ox != scrollX_ || oy != scrollY_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::zoomAt(POINT client, int steps) {
    if (!doc_ || steps == 0) return;
    // Keep the page point under the cursor where it is.
    Pos p;
    bool onPage = pageAt(client, p, true);
    float z = std::clamp(zoom_ * std::pow(kZoomStep, static_cast<float>(steps)), kMinZoom, kMaxZoom);
    if (z == zoom_) return;
    zoom_ = z;
    fit_ = false;
    freeTiles();
    layout();
    if (onPage) {
        fz_rect b = doc_->bounds(p.page);
        int dx = pageX(p.page) + static_cast<int>((p.pt.x - b.x0) * zoom_);
        int dy = slots_[static_cast<size_t>(p.page)].y + static_cast<int>((p.pt.y - b.y0) * zoom_);
        scrollX_ = dx - client.x;
        scrollY_ = dy - (client.y - barH_);
    }
    updateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Page under a client point. With clamp, points between or beyond pages map to
// the nearest page, and coordinates are clamped to it.
bool Viewer::pageAt(POINT client, Pos& out, bool clamp) const {
    if (!doc_ || slots_.empty()) return false;
    int y = client.y - barH_ + scrollY_;
    int x = client.x + scrollX_;
    auto it = std::upper_bound(slots_.begin(), slots_.end(), y, [&](int v, const Slot& s) {
        return v < s.y + s.h + gap_ / 2;
    });
    int i = static_cast<int>(std::min<size_t>(static_cast<size_t>(it - slots_.begin()), slots_.size() - 1));
    const Slot& s = slots_[static_cast<size_t>(i)];
    int px = x - pageX(i), py = y - s.y;
    bool inside = px >= 0 && py >= 0 && px < s.w && py < s.h;
    if (!inside && !clamp) return false;
    px = std::clamp(px, 0, s.w);
    py = std::clamp(py, 0, s.h);
    fz_rect b = doc_->bounds(i);
    out.page = i;
    out.pt = fz_make_point(b.x0 + px / zoom_, b.y0 + py / zoom_);
    return true;
}

// ------------------------------------------------------------------ painting

void Viewer::freeTiles() {
    for (auto& t : tiles_)
        if (t.bmp) DeleteObject(t.bmp);
    tiles_.clear();
}

// Returns a rendered tile of `page` that covers `need` (page pixels).
const Viewer::Tile* Viewer::tileFor(int page, const RECT& need) {
    for (auto& t : tiles_) {
        if (t.page == page && t.zoom == zoom_ && contains(t.area, need)) {
            t.use = ++tileClock_;
            return &t;
        }
    }
    // Render the needed part plus half a screen above and below, so that
    // scrolling reuses the tile for a while.
    const Slot& s = slots_[static_cast<size_t>(page)];
    int margin = viewHeight() / 2;
    RECT area{need.left, std::max(0L, need.top - margin), need.right, std::min<LONG>(s.h, need.bottom + margin)};
    int w = area.right - area.left, h = area.bottom - area.top;
    if (w <= 0 || h <= 0) return nullptr;

    Tile* slot = nullptr;
    for (auto& t : tiles_)
        if (t.page == page) slot = &t;
    if (!slot && tiles_.size() < kMaxTiles) slot = &tiles_.emplace_back();
    if (!slot)
        slot = &*std::min_element(tiles_.begin(), tiles_.end(),
                                  [](const Tile& a, const Tile& b) { return a.use < b.use; });
    if (slot->bmp) DeleteObject(slot->bmp);
    *slot = Tile{};

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return nullptr;
    int stride = (w * 3 + 3) & ~3;
    doc_->render(page, zoom_, fz_make_irect(area.left, area.top, area.right, area.bottom),
                 static_cast<unsigned char*>(bits), stride);
    GdiFlush();
    slot->page = page;
    slot->zoom = zoom_;
    slot->area = area;
    slot->bmp = bmp;
    slot->use = ++tileClock_;
    return slot;
}

void Viewer::paint(HDC dc) {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    if (!back_ || backSize_.cx != rc.right || backSize_.cy != rc.bottom) {
        if (back_) DeleteObject(back_);
        back_ = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        backSize_ = {rc.right, rc.bottom};
    }
    HDC mem = CreateCompatibleDC(dc);
    HDC tileDc = CreateCompatibleDC(dc);
    HGDIOBJ oldBack = SelectObject(mem, back_);
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    if (doc_) {
        int vh = viewHeight();
        auto first = std::upper_bound(slots_.begin(), slots_.end(), scrollY_,
                                      [](int v, const Slot& s) { return v < s.y + s.h; });
        for (auto it = first; it != slots_.end() && it->y < scrollY_ + vh; ++it) {
            int i = static_cast<int>(it - slots_.begin());
            int sx = pageX(i) - scrollX_, sy = barH_ + it->y - scrollY_;  // page origin on screen
            RECT need{std::max(0, -sx), std::max(0, barH_ - sy), std::min(it->w, static_cast<int>(rc.right) - sx),
                      std::min(it->h, static_cast<int>(rc.bottom) - sy)};
            if (need.right <= need.left || need.bottom <= need.top) continue;
            if (const Tile* t = tileFor(i, need)) {
                HGDIOBJ old = SelectObject(tileDc, t->bmp);
                BitBlt(mem, sx + need.left, sy + need.top, need.right - need.left, need.bottom - need.top, tileDc,
                       need.left - t->area.left, need.top - t->area.top, SRCCOPY);
                SelectObject(tileDc, old);
            }
            drawSelection(mem, i, sx, sy);
        }
    }

    if (barH_ > 0) {
        RECT bar{0, 0, rc.right, barH_};
        HBRUSH b = CreateSolidBrush(kBarBack);
        FillRect(mem, &bar, b);
        DeleteObject(b);
        HGDIOBJ oldFont = SelectObject(mem, barFont_);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(0, 0, 0));
        RECT text{gap_, 0, rc.right - gap_, barH_};
        DrawTextW(mem, barText_.c_str(), static_cast<int>(barText_.size()), &text,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(mem, oldFont);
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBack);
    DeleteDC(tileDc);
    DeleteDC(mem);
}

// Warnings appear while pages are rendered (e.g. the first JPEG 2000 image).
void Viewer::updateBar() {
    unsigned flags = doc_ ? doc_->warnings().flags & ~barHidden_ : 0;
    if (flags == barShown_) return;
    barShown_ = flags;
    std::wstring t;
    auto add = [&](const std::wstring& s) { t += (t.empty() ? L"" : L"  •  ") + s; };
    if (flags & TCL_WARN_JPX) add(L"JPEG 2000 images are not shown");
    if (flags & TCL_WARN_ANNOT) add(L"Annotations or form fields without an appearance are not shown");
    if (flags & TCL_WARN_CMAP)
        add(L"CJK encoding " + toWide(doc_->warnings().cmap) + L" is not built in, some text may be missing");
    if (flags & TCL_WARN_FONT)
        add(L"Font " + toWide(doc_->warnings().font) + L" is not available, some text may be missing");
    barText_ = t.empty() ? t : t + L"   (click to hide)";

    int h = 0;
    if (!barText_.empty()) {
        HDC dc = GetDC(hwnd_);
        HGDIOBJ old = SelectObject(dc, barFont_);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(hwnd_, dc);
        h = tm.tmHeight + MulDiv(gap_, 3, 4);
    }
    if (h != barH_) {
        barH_ = h;
        layout();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ------------------------------------------------------------------ selection

fz_point Viewer::topLeft(int page) const {
    fz_rect b = doc_->bounds(page);
    return fz_make_point(b.x0, b.y0);
}

fz_point Viewer::bottomRight(int page) const {
    fz_rect b = doc_->bounds(page);
    return fz_make_point(b.x1, b.y1);
}

bool Viewer::selectionOn(int page, fz_point& a, fz_point& b) const {
    if (!hasSel_) return false;
    const Pos& s = anchor_.page <= focus_.page ? anchor_ : focus_;
    const Pos& e = anchor_.page <= focus_.page ? focus_ : anchor_;
    if (page < s.page || page > e.page) return false;
    if (s.page == e.page) {
        a = anchor_.pt;  // MuPDF orders the two points itself
        b = focus_.pt;
    } else {
        a = page == s.page ? s.pt : topLeft(page);
        b = page == e.page ? e.pt : bottomRight(page);
    }
    return true;
}

void Viewer::selectionChanged() {
    quads_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::drawSelection(HDC dc, int page, int px, int py) {
    fz_point a, b;
    if (!selectionOn(page, a, b)) return;
    auto it = quads_.find(page);
    if (it == quads_.end()) {
        std::vector<fz_quad> q;
        doc_->highlight(page, a, b, q);
        it = quads_.emplace(page, std::move(q)).first;
    }
    if (it->second.empty()) return;
    fz_rect r = doc_->bounds(page);
    auto map = [&](fz_point p) {
        return POINT{px + static_cast<LONG>(std::lround((p.x - r.x0) * zoom_)),
                     py + static_cast<LONG>(std::lround((p.y - r.y0) * zoom_))};
    };
    HBRUSH brush = CreateSolidBrush(kSelection);
    HPEN pen = CreatePen(PS_SOLID, 1, kSelection);
    HGDIOBJ ob = SelectObject(dc, brush), op = SelectObject(dc, pen);
    int rop = SetROP2(dc, R2_MASKPEN);
    for (const fz_quad& q : it->second) {
        POINT pts[4] = {map(q.ul), map(q.ur), map(q.lr), map(q.ll)};
        Polygon(dc, pts, 4);
    }
    SetROP2(dc, rop);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(brush);
    DeleteObject(pen);
}

void Viewer::selectAll() {
    if (!doc_) return;
    anchor_ = {0, topLeft(0)};
    int last = doc_->pageCount() - 1;
    focus_ = {last, bottomRight(last)};
    hasSel_ = true;
    selectionChanged();
}

void Viewer::copy() {
    if (!doc_ || !hasSel_) return;
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::string text;
    int first = std::min(anchor_.page, focus_.page), last = std::max(anchor_.page, focus_.page);
    for (int i = first; i <= last; i++) {
        fz_point a, b;
        if (!selectionOn(i, a, b)) continue;
        std::string s = doc_->copy(i, a, b);
        if (!text.empty() && !s.empty() && text.back() != '\n') text += "\r\n";
        text += s;
    }
    SetCursor(old);
    std::wstring w = toWide(text);
    if (w.empty() || !OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, w.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

void Viewer::endDrag() {
    if (drag_ == Drag::None) return;
    drag_ = Drag::None;
    KillTimer(hwnd_, kDragTimer);
    if (GetCapture() == hwnd_) ReleaseCapture();
}

void Viewer::updateCursor() {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd_, &pt);
    Pos p;
    LPCWSTR cur = IDC_ARROW;
    if (drag_ == Drag::Pan || pt.y < barH_) cur = IDC_HAND;
    else if (drag_ == Drag::Select) cur = IDC_IBEAM;
    else if (pageAt(pt, p, false)) cur = GetKeyState(VK_MENU) < 0 ? IDC_IBEAM : IDC_HAND;
    SetCursor(LoadCursor(nullptr, cur));
}

void Viewer::dragTo(POINT client) {
    Pos p;
    if (!pageAt(client, p, true)) return;
    if (!hasSel_ && p.page == anchor_.page && std::abs(p.pt.x - anchor_.pt.x) < 1 &&
        std::abs(p.pt.y - anchor_.pt.y) < 1)
        return;
    focus_ = p;
    hasSel_ = true;
    selectionChanged();
}

// ------------------------------------------------------------------ messages

LRESULT CALLBACK Viewer::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    try {
        if (msg == WM_NCCREATE) {
            auto* v = new Viewer(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(v));
        }
        Viewer* v = from(hwnd);
        if (msg == WM_NCDESTROY) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete v;
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        if (v) return v->handle(msg, wp, lp);
    } catch (...) {
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Viewer::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            paint(dc);
            EndPaint(hwnd_, &ps);
            updateBar();
            return 0;
        }
        case WM_PRINTCLIENT:
            paint(reinterpret_cast<HDC>(wp));
            updateBar();
            return 0;
        case WM_SIZE:
            if (!inLayout_) {
                if (fit_) freeTiles();
                layout();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;

        case WM_VSCROLL:
        case WM_HSCROLL: {
            bool vert = msg == WM_VSCROLL;
            SCROLLINFO si{sizeof si, SIF_ALL};
            GetScrollInfo(hwnd_, vert ? SB_VERT : SB_HORZ, &si);
            int pos = vert ? scrollY_ : scrollX_;
            int page = static_cast<int>(si.nPage);
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= line_; break;
                case SB_LINEDOWN: pos += line_; break;
                case SB_PAGEUP: pos -= std::max(line_, page - line_); break;
                case SB_PAGEDOWN: pos += std::max(line_, page - line_); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                case SB_TOP: pos = 0; break;
                case SB_BOTTOM: pos = si.nMax; break;
                default: return 0;
            }
            if (vert) scrollTo(scrollX_, pos);
            else scrollTo(pos, scrollY_);
            return 0;
        }
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            bool horizontal = msg == WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT);
            if (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)) {
                zoomWheel_ += delta;
                int steps = zoomWheel_ / WHEEL_DELTA;
                zoomWheel_ -= steps * WHEEL_DELTA;
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd_, &pt);
                zoomAt(pt, steps);
                return 0;
            }
            UINT lines = 3;
            SystemParametersInfoW(horizontal ? SPI_GETWHEELSCROLLCHARS : SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            int& acc = horizontal ? hwheel_ : wheel_;
            acc += msg == WM_MOUSEHWHEEL ? -delta : delta;
            int px = lines == WHEEL_PAGESCROLL ? viewHeight() * acc / WHEEL_DELTA
                                               : static_cast<int>(lines) * line_ / 2 * acc / WHEEL_DELTA;
            if (px == 0) return 0;
            acc = 0;
            if (horizontal) scrollTo(scrollX_ - px, scrollY_);
            else scrollTo(scrollX_, scrollY_ - px);
            return 0;
        }
        case WM_KEYDOWN: {
            bool ctrl = GetKeyState(VK_CONTROL) < 0;
            int vh = viewHeight();
            switch (wp) {
                case VK_UP: scrollTo(scrollX_, scrollY_ - line_); return 0;
                case VK_DOWN: scrollTo(scrollX_, scrollY_ + line_); return 0;
                case VK_LEFT: scrollTo(scrollX_ - line_, scrollY_); return 0;
                case VK_RIGHT: scrollTo(scrollX_ + line_, scrollY_); return 0;
                case VK_PRIOR: scrollTo(scrollX_, scrollY_ - std::max(line_, vh - line_)); return 0;
                case VK_NEXT:
                case VK_SPACE: scrollTo(scrollX_, scrollY_ + std::max(line_, vh - line_)); return 0;
                case VK_HOME: scrollTo(scrollX_, 0); return 0;
                case VK_END: scrollTo(scrollX_, docH_); return 0;
                case 'C':
                case VK_INSERT:
                    if (ctrl) { copy(); return 0; }
                    break;
                case 'A':
                    if (ctrl) { selectAll(); return 0; }
                    break;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (pt.y < barH_) {
                barHidden_ |= barShown_;
                updateBar();
                return 0;
            }
            endDrag();
            if (GetKeyState(VK_MENU) < 0) {
                // Alt+drag: select text.
                altUsed_ = true;
                bool had = hasSel_;
                hasSel_ = false;
                if (pageAt(pt, anchor_, true)) {
                    focus_ = anchor_;
                    drag_ = Drag::Select;
                    dragPoint_ = pt;
                    SetCapture(hwnd_);
                    SetTimer(hwnd_, kDragTimer, 50, nullptr);
                }
                if (had) selectionChanged();
            } else {
                // Drag: pan. A click without moving clears the selection (on button up).
                drag_ = Drag::Pan;
                moved_ = false;
                panStart_ = pt;
                panScrollX_ = scrollX_;
                panScrollY_ = scrollY_;
                SetCapture(hwnd_);
            }
            updateCursor();
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            // Select the word under the cursor (the first click already started a pan).
            endDrag();
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (pt.y < barH_) return 0;
            ignoreUp_ = true;
            if (GetKeyState(VK_MENU) < 0) altUsed_ = true;
            Pos p;
            fz_point a, b;
            if (pageAt(pt, p, false) && doc_->wordAt(p.page, p.pt, a, b)) {
                anchor_ = {p.page, a};
                focus_ = {p.page, b};
                hasSel_ = true;
                selectionChanged();
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (drag_ == Drag::Select) {
                dragPoint_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                dragTo(dragPoint_);
            } else if (drag_ == Drag::Pan) {
                int dx = GET_X_LPARAM(lp) - panStart_.x, dy = GET_Y_LPARAM(lp) - panStart_.y;
                if (dx || dy) moved_ = true;
                scrollTo(panScrollX_ - dx, panScrollY_ - dy);
            }
            return 0;
        case WM_TIMER:
            if (wp == kDragTimer && drag_ == Drag::Select) {
                // Auto-scroll while dragging above or below the view.
                RECT rc;
                GetClientRect(hwnd_, &rc);
                int dy = dragPoint_.y < barH_ ? dragPoint_.y - barH_ : dragPoint_.y > rc.bottom ? dragPoint_.y - rc.bottom : 0;
                int dx = dragPoint_.x < 0 ? dragPoint_.x : dragPoint_.x > rc.right ? dragPoint_.x - rc.right : 0;
                if (dx || dy) {
                    scrollTo(scrollX_ + dx, scrollY_ + dy);
                    dragTo(dragPoint_);
                }
            }
            return 0;
        case WM_LBUTTONUP: {
            bool click = drag_ == Drag::Pan && !moved_ && !ignoreUp_;
            endDrag();
            ignoreUp_ = false;
            if (click && hasSel_) {
                hasSel_ = false;
                selectionChanged();
            }
            updateCursor();
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != hwnd_) {
                drag_ = Drag::None;
                KillTimer(hwnd_, kDragTimer);
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                updateCursor();
                return TRUE;
            }
            break;
        case WM_SYSKEYDOWN:
            if (wp == VK_MENU) updateCursor();
            break;
        case WM_SYSKEYUP:
            if (wp == VK_MENU) {
                updateCursor();
                // Alt was used for selecting: releasing it must not open Lister's menu.
                if (altUsed_) {
                    altUsed_ = false;
                    return 0;
                }
            }
            break;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace mupdfv
