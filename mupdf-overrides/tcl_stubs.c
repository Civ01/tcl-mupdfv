// Stand-ins for MuPDF files left out of the build.
//
//   load-jpx.c        JPEG 2000 images: skipped, reported as TCL_WARN_JPX.
//   pdf-appearance.c  Appearance synthesis for annotations and form fields
//                     without an appearance stream: skipped, reported as
//                     TCL_WARN_ANNOT. Annotations that carry their own
//                     appearance stream (the normal case) render as usual.
//   load-tiff.c, load-bmp.c, load-pnm.c
//                     Standalone image formats. PDF never uses them.
#include "tcl_mupdf.h"

#include "mupdf/pdf.h"
#include "pdf-annot-imp.h"

#include <string.h>

void tcl_warn(fz_context *ctx, unsigned flag, const char *detail)
{
	tcl_warnings *w = (tcl_warnings *)fz_user_context(ctx);
	char *dst = NULL;
	size_t cap = 0;
	if (!w)
		return;
	if (flag == TCL_WARN_CMAP)
		dst = w->cmap, cap = sizeof w->cmap;
	else if (flag == TCL_WARN_FONT)
		dst = w->font, cap = sizeof w->font;
	if (dst && detail && !(w->flags & flag))
		fz_strlcpy(dst, detail, cap);
	w->flags |= flag;
}

/* ---- load-jpx.c ---- */

fz_pixmap *
fz_load_jpx(fz_context *ctx, const unsigned char *data, size_t size, fz_colorspace *defcs)
{
	tcl_warn(ctx, TCL_WARN_JPX, NULL);
	fz_throw(ctx, FZ_ERROR_FORMAT, "JPEG 2000 images are not supported");
}

void
fz_load_jpx_info(fz_context *ctx, const unsigned char *data, size_t size, int *wp, int *hp, int *xresp, int *yresp, fz_colorspace **cspacep)
{
	tcl_warn(ctx, TCL_WARN_JPX, NULL);
	fz_throw(ctx, FZ_ERROR_FORMAT, "JPEG 2000 images are not supported");
}

/* ---- load-tiff.c, load-bmp.c, load-pnm.c ---- */

#define NO_IMAGE(fmt) \
	fz_pixmap *fz_load_##fmt(fz_context *ctx, const unsigned char *p, size_t n) \
	{ fz_throw(ctx, FZ_ERROR_UNSUPPORTED, #fmt " images are not supported"); } \
	fz_pixmap *fz_load_##fmt##_subimage(fz_context *ctx, const unsigned char *p, size_t n, int i) \
	{ fz_throw(ctx, FZ_ERROR_UNSUPPORTED, #fmt " images are not supported"); } \
	void fz_load_##fmt##_info(fz_context *ctx, const unsigned char *p, size_t n, int *w, int *h, int *xres, int *yres, fz_colorspace **cs) \
	{ fz_throw(ctx, FZ_ERROR_UNSUPPORTED, #fmt " images are not supported"); } \
	void fz_load_##fmt##_info_subimage(fz_context *ctx, const unsigned char *p, size_t n, int *w, int *h, int *xres, int *yres, fz_colorspace **cs, int i) \
	{ fz_throw(ctx, FZ_ERROR_UNSUPPORTED, #fmt " images are not supported"); } \
	int fz_load_##fmt##_subimage_count(fz_context *ctx, const unsigned char *p, size_t n) \
	{ return 0; }

NO_IMAGE(tiff)
NO_IMAGE(bmp)
NO_IMAGE(pnm)

/* ---- pdf-appearance.c ---- */

void
pdf_annot_push_local_xref(fz_context *ctx, pdf_annot *annot)
{
	if (!annot->page)
		fz_throw(ctx, FZ_ERROR_ARGUMENT, "annotation not bound to any page");
	annot->page->doc->local_xref_nesting++;
}

void
pdf_annot_ensure_local_xref(fz_context *ctx, pdf_annot *annot)
{
	pdf_document *doc = annot->page->doc;
	if (doc->local_xref == NULL)
		doc->local_xref = pdf_new_local_xref(ctx, doc);
}

void
pdf_annot_pop_local_xref(fz_context *ctx, pdf_annot *annot)
{
	--annot->page->doc->local_xref_nesting;
}

void
pdf_annot_pop_and_discard_local_xref(fz_context *ctx, pdf_annot *annot)
{
	pdf_document *doc = annot->page->doc;
	--doc->local_xref_nesting;
	pdf_drop_local_xref_and_resources(ctx, doc);
}

fz_rect
pdf_annot_rect_diff(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *rd_obj = pdf_dict_get(ctx, annot->obj, PDF_NAME(RD));
	fz_rect rd;
	if (!pdf_is_array(ctx, rd_obj))
		return fz_make_rect(0, 0, 0, 0);
	rd.x0 = pdf_array_get_real(ctx, rd_obj, 0);
	rd.y0 = pdf_array_get_real(ctx, rd_obj, 1);
	rd.x1 = pdf_array_get_real(ctx, rd_obj, 2);
	rd.y1 = pdf_array_get_real(ctx, rd_obj, 3);
	return rd;
}

// Called for every annotation and widget when a page is loaded or run.
// MuPDF would synthesize missing appearances here; we only report them.
int
pdf_update_annot(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *subtype;
	if (!annot->page)
		fz_throw(ctx, FZ_ERROR_ARGUMENT, "annotation not bound to any page");
	annot->page->doc->resynth_required = 0;
	annot->has_new_ap = 0;

	subtype = pdf_dict_get(ctx, annot->obj, PDF_NAME(Subtype));
	if (subtype == PDF_NAME(Popup) || subtype == PDF_NAME(Link))
		return 0;
	if (pdf_annot_flags(ctx, annot) & (PDF_ANNOT_IS_HIDDEN | PDF_ANNOT_IS_NO_VIEW))
		return 0;
	if (!pdf_annot_ap(ctx, annot))
		tcl_warn(ctx, TCL_WARN_ANNOT, NULL);
	return 0;
}

fz_layout_block *
pdf_layout_text_widget(fz_context *ctx, pdf_annot *annot)
{
	fz_throw(ctx, FZ_ERROR_UNSUPPORTED, "appearance synthesis is not supported");
}

char *
pdf_signature_info(fz_context *ctx, const char *name, pdf_pkcs7_distinguished_name *dn, const char *reason, const char *location, int64_t date, int include_labels)
{
	fz_throw(ctx, FZ_ERROR_UNSUPPORTED, "signing is not supported");
}

fz_display_list *
pdf_signature_appearance_signed(fz_context *ctx, fz_rect rect, fz_text_language lang, fz_image *img, const char *left_text, const char *right_text, int include_logo)
{
	fz_throw(ctx, FZ_ERROR_UNSUPPORTED, "signing is not supported");
}

fz_display_list *
pdf_signature_appearance_unsigned(fz_context *ctx, fz_rect rect, fz_text_language lang)
{
	fz_throw(ctx, FZ_ERROR_UNSUPPORTED, "signing is not supported");
}
