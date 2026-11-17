// A PDF document and the MuPDF context it lives in. Every MuPDF call of the
// plugin is in Document.cpp; nothing here throws.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mupdf/pdf.h"
#include "tcl_mupdf.h"

namespace mupdfv {

class Document {
public:
    // Returns nullptr if the file can't be opened as a PDF (or needs a password).
    static std::unique_ptr<Document> open(const std::string& utf8Path);
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    int pageCount() const { return static_cast<int>(pages_.size()); }
    // Page rectangle in points (MuPDF page space: y down, rotation applied).
    fz_rect bounds(int page) const { return pages_[page].bounds; }

    // Renders the pixel rectangle `area` of `page` at `zoom` pixels per point
    // (page top-left = 0,0) into 24-bit BGR rows. Returns false on failure; the
    // buffer is then left white.
    bool render(int page, float zoom, fz_irect area, unsigned char* bits, int stride);

    // Highlight quads (page space) of the text between a and b, in reading order.
    void highlight(int page, fz_point a, fz_point b, std::vector<fz_quad>& out);
    // Text between a and b, UTF-8 with CRLF line ends.
    std::string copy(int page, fz_point a, fz_point b);

    const tcl_warnings& warnings() const { return warn_; }

private:
    struct Page {
        fz_rect bounds{};
        fz_display_list* list = nullptr;
        fz_stext_page* text = nullptr;
        bool failed = false;
        unsigned lastUse = 0;
    };

    Document() = default;
    fz_display_list* list(int page);
    fz_stext_page* text(int page);
    void trimCaches(int keep);

    fz_context* ctx_ = nullptr;
    pdf_document* doc_ = nullptr;
    tcl_warnings warn_{};
    std::vector<Page> pages_;
    unsigned useClock_ = 0;
};

}  // namespace mupdfv
