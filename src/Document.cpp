#include "Document.h"

#include <cstring>

#include "mupdf/pdf.h"

#ifdef _MSC_VER
#pragma warning(disable : 4611)  // setjmp and C++ destructors: see below
#endif
// MuPDF reports errors with setjmp/longjmp (fz_try/fz_catch). No C++ object with
// a destructor may live inside those blocks, and locals changed inside fz_try
// must be marked with fz_var. The functions below follow that rule.

namespace mupdfv {

namespace {
constexpr size_t kStore = 64u << 20;  // MuPDF resource cache per document
constexpr int kCachedPages = 24;      // pages that keep their display list / text

// Page rectangle without loading the page; a US Letter page if it is unusable.
fz_rect pageBox(fz_context* ctx, pdf_document* doc, int page) {
    fz_rect r = fz_empty_rect;
    fz_var(r);
    fz_try(ctx) {
        fz_rect box;
        fz_matrix ctm;
        pdf_page_obj_transform(ctx, pdf_lookup_page_obj(ctx, doc, page), &box, &ctm);
        r = fz_transform_rect(box, ctm);
    }
    fz_catch(ctx) { fz_ignore_error(ctx); }
    if (fz_is_empty_rect(r) || r.x1 - r.x0 > 14400 || r.y1 - r.y0 > 14400) r = fz_make_rect(0, 0, 612, 792);
    return r;
}
}  // namespace

std::unique_ptr<Document> Document::open(const std::string& utf8Path) {
    std::unique_ptr<Document> d(new Document());
    fz_context* ctx = fz_new_context(nullptr, nullptr, kStore);
    if (!ctx) return nullptr;
    d->ctx_ = ctx;
    fz_set_user_context(ctx, &d->warn_);
    fz_set_error_callback(ctx, nullptr, nullptr);
    fz_set_warning_callback(ctx, nullptr, nullptr);
    tcl_install_fonts(ctx);

    pdf_document* doc = nullptr;
    int count = 0;
    fz_var(doc);
    fz_var(count);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, utf8Path.c_str());
        if (pdf_needs_password(ctx, doc) && !pdf_authenticate_password(ctx, doc, ""))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "password required");
        count = pdf_count_pages(ctx, doc);
    }
    fz_catch(ctx) {
        pdf_drop_document(ctx, doc);
        doc = nullptr;
    }
    if (!doc || count <= 0) {
        pdf_drop_document(ctx, doc);
        return nullptr;  // ~Document drops the context
    }
    d->doc_ = doc;
    d->pages_.resize(static_cast<size_t>(count));

    // Page sizes for the layout, without loading the pages.
    fz_try(ctx) { pdf_load_page_tree(ctx, doc); }
    fz_catch(ctx) { fz_ignore_error(ctx); }
    for (int i = 0; i < count; i++) d->pages_[static_cast<size_t>(i)].bounds = pageBox(ctx, doc, i);
    return d;
}

Document::~Document() {
    if (!ctx_) return;
    for (auto& p : pages_) {
        fz_drop_stext_page(ctx_, p.text);
        fz_drop_display_list(ctx_, p.list);
    }
    pdf_drop_document(ctx_, doc_);
    fz_drop_context(ctx_);
}

void Document::trimCaches(int keep) {
    int cached = 0;
    for (auto& p : pages_) cached += (p.list || p.text) ? 1 : 0;
    while (cached > kCachedPages) {
        Page* oldest = nullptr;
        for (int i = 0; i < pageCount(); i++) {
            Page& p = pages_[static_cast<size_t>(i)];
            if (i != keep && (p.list || p.text) && (!oldest || p.lastUse < oldest->lastUse)) oldest = &p;
        }
        if (!oldest) break;
        fz_drop_stext_page(ctx_, oldest->text);
        fz_drop_display_list(ctx_, oldest->list);
        oldest->text = nullptr;
        oldest->list = nullptr;
        cached--;
    }
}

fz_display_list* Document::list(int page) {
    if (page < 0 || page >= pageCount()) return nullptr;
    Page& p = pages_[static_cast<size_t>(page)];
    p.lastUse = ++useClock_;
    if (p.list || p.failed) return p.list;
    fz_page* fp = nullptr;
    fz_display_list* l = nullptr;
    fz_var(fp);
    fz_var(l);
    fz_try(ctx_) {
        fp = fz_load_page(ctx_, &doc_->super, page);
        l = fz_new_display_list_from_page(ctx_, fp);
    }
    fz_always(ctx_) { fz_drop_page(ctx_, fp); }
    fz_catch(ctx_) {
        fz_ignore_error(ctx_);
        l = nullptr;
    }
    p.list = l;
    p.failed = !l;
    trimCaches(page);
    return p.list;
}

fz_stext_page* Document::text(int page) {
    fz_display_list* l = list(page);
    if (!l) return nullptr;
    Page& p = pages_[static_cast<size_t>(page)];
    if (p.text) return p.text;
    fz_stext_page* t = nullptr;
    fz_var(t);
    fz_try(ctx_) { t = fz_new_stext_page_from_display_list(ctx_, l, nullptr); }
    fz_catch(ctx_) {
        fz_ignore_error(ctx_);
        t = nullptr;
    }
    p.text = t;
    return t;
}

bool Document::render(int page, float zoom, fz_irect area, unsigned char* bits, int stride) {
    int w = area.x1 - area.x0, h = area.y1 - area.y0;
    if (w <= 0 || h <= 0) return false;
    std::memset(bits, 0xFF, static_cast<size_t>(stride) * static_cast<size_t>(h));
    fz_display_list* l = list(page);
    if (!l) return false;

    fz_rect b = pages_[static_cast<size_t>(page)].bounds;
    fz_matrix ctm = fz_concat(fz_translate(-b.x0, -b.y0), fz_scale(zoom, zoom));
    fz_pixmap* pix = nullptr;
    fz_device* dev = nullptr;
    bool ok = false;
    fz_var(pix);
    fz_var(dev);
    fz_var(ok);
    fz_try(ctx_) {
        pix = fz_new_pixmap_with_data(ctx_, fz_device_bgr(ctx_), w, h, nullptr, 0, stride, bits);
        pix->x = area.x0;
        pix->y = area.y0;
        dev = fz_new_draw_device(ctx_, fz_identity, pix);
        fz_run_display_list(ctx_, l, dev, ctm, fz_rect_from_irect(area), nullptr);
        fz_close_device(ctx_, dev);
        ok = true;
    }
    fz_always(ctx_) {
        fz_drop_device(ctx_, dev);
        fz_drop_pixmap(ctx_, pix);  // does not free `bits`
    }
    fz_catch(ctx_) { fz_ignore_error(ctx_); }
    return ok;
}

void Document::highlight(int page, fz_point a, fz_point b, std::vector<fz_quad>& out) {
    out.clear();
    fz_stext_page* t = text(page);
    if (!t) return;
    for (int cap = 256; cap <= 65536; cap *= 4) {
        out.resize(static_cast<size_t>(cap));
        int n = 0;
        fz_var(n);
        fz_try(ctx_) { n = fz_highlight_selection(ctx_, t, a, b, out.data(), cap); }
        fz_catch(ctx_) {
            fz_ignore_error(ctx_);
            n = 0;
        }
        if (n < cap) {
            out.resize(static_cast<size_t>(n));
            return;
        }
    }
}

bool Document::wordAt(int page, fz_point pt, fz_point& a, fz_point& b) {
    fz_stext_page* t = text(page);
    if (!t) return false;
    fz_point pa = pt, pb = pt;
    fz_quad q{};
    bool ok = false;
    fz_var(ok);
    fz_try(ctx_) {
        q = fz_snap_selection(ctx_, t, &pa, &pb, FZ_SELECT_WORDS);
        ok = true;
    }
    fz_catch(ctx_) { fz_ignore_error(ctx_); }
    if (!ok) return false;
    // After the last character on the page MuPDF leaves the end point unchanged.
    if ((pa.x == pt.x && pa.y == pt.y) || (pb.x == pt.x && pb.y == pt.y)) return false;
    // MuPDF snaps to the nearest character however far away it is; only
    // accept the word if the point is on it.
    fz_rect r = fz_expand_rect(fz_rect_from_quad(q), 1.0f);
    if (fz_is_empty_rect(r) || !fz_is_point_inside_rect(pt, r)) return false;
    a = pa;
    b = pb;
    return true;
}

std::string Document::copy(int page, fz_point a, fz_point b) {
    fz_stext_page* t = text(page);
    if (!t) return {};
    char* s = nullptr;
    fz_var(s);
    fz_try(ctx_) { s = fz_copy_selection(ctx_, t, a, b, 1); }
    fz_catch(ctx_) {
        fz_ignore_error(ctx_);
        s = nullptr;
    }
    if (!s) return {};
    std::string r;
    try {
        r = s;
    } catch (...) {
        fz_free(ctx_, s);
        throw;
    }
    fz_free(ctx_, s);
    return r;
}

}  // namespace mupdfv
