// Renders the sample PDFs with the trimmed MuPDF and checks pixels, the text
// that selection/copy returns, and the warnings the overrides report.
// Usage: core_test <samples dir>
// On non-Windows hosts, TCL_MUPDFV_FONT_DIR must point to a folder with
// stand-ins for arial.ttf, times.ttf, ... (CMake sets this up).
#include "tcl_mupdf.h"
#include "mupdf/pdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

typedef struct {
	fz_context *ctx;
	tcl_warnings w;
	int pages;
	fz_pixmap *first;  // page 1 at 72 dpi, RGB
	char text[4096];   // all text, as fz_copy_selection returns it
} result;

static void run(const char *dir, const char *name, result *r)
{
	char path[1024];
	pdf_document *doc = NULL;
	fz_page *page = NULL;
	fz_stext_page *st = NULL;
	char *s = NULL;
	int i;
	memset(r, 0, sizeof *r);
	snprintf(path, sizeof path, "%s/%s", dir, name);
	r->ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	fz_set_user_context(r->ctx, &r->w);
	fz_set_warning_callback(r->ctx, NULL, NULL);
	tcl_install_fonts(r->ctx);
	fz_var(doc); fz_var(page); fz_var(st); fz_var(s);
	fz_try(r->ctx)
	{
		doc = pdf_open_document(r->ctx, path);
		r->pages = pdf_count_pages(r->ctx, doc);
		for (i = 0; i < r->pages; i++)
		{
			fz_rect b;
			page = fz_load_page(r->ctx, (fz_document *)doc, i);
			b = fz_bound_page(r->ctx, page);
			if (i == 0)
				r->first = fz_new_pixmap_from_page(r->ctx, page, fz_identity, fz_device_rgb(r->ctx), 0);
			st = fz_new_stext_page_from_page(r->ctx, page, NULL);
			s = fz_copy_selection(r->ctx, st, fz_make_point(b.x0, b.y0), fz_make_point(b.x1, b.y1), 0);
			if (strlen(r->text) + strlen(s) + 2 < sizeof r->text)
				strcat(strcat(r->text, s), "\n");
			fz_free(r->ctx, s); s = NULL;
			fz_drop_stext_page(r->ctx, st); st = NULL;
			fz_drop_page(r->ctx, page); page = NULL;
		}
	}
	fz_always(r->ctx)
	{
		fz_free(r->ctx, s);
		fz_drop_stext_page(r->ctx, st);
		fz_drop_page(r->ctx, page);
		pdf_drop_document(r->ctx, doc);
	}
	fz_catch(r->ctx)
	{
		CHECK(0, "%s: %s", name, fz_caught_message(r->ctx));
	}
}

static void done(result *r)
{
	fz_drop_pixmap(r->ctx, r->first);
	fz_drop_context(r->ctx);
}

// Colour at PDF user-space point (x, y) of page 1 (page height 842).
static int pixel_is(result *r, int x, int y, int cr, int cg, int cb)
{
	unsigned char *p;
	if (!r->first)
		return 0;
	p = r->first->samples + (842 - y) * r->first->stride + x * r->first->n;
	return abs(p[0] - cr) < 8 && abs(p[1] - cg) < 8 && abs(p[2] - cb) < 8;
}

// Whether any pixel in the PDF-space box is dark (i.e. glyphs were drawn).
static int has_ink(result *r, int x0, int y0, int x1, int y1)
{
	int x, y;
	if (!r->first)
		return 0;
	for (y = 842 - y1; y < 842 - y0; y++)
		for (x = x0; x < x1; x++)
			if (r->first->samples[y * r->first->stride + x * r->first->n] < 100)
				return 1;
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "tests/samples";
	result r;

	run(dir, "text.pdf", &r);
	CHECK(r.pages == 2, "text.pdf pages=%d", r.pages);
	CHECK(r.w.flags == 0, "text.pdf flags=%u font=%s", r.w.flags, r.w.font);
	CHECK(strstr(r.text, "Hello Helvetica") != NULL, "text: %s", r.text);
	CHECK(strstr(r.text, "Times Roman line") != NULL, "text: %s", r.text);
	CHECK(strstr(r.text, "Courier mono") != NULL, "text: %s", r.text);
	CHECK(strstr(r.text, "Bold Helvetica") != NULL, "text: %s", r.text);
	CHECK(strstr(r.text, "Second page text") != NULL, "text: %s", r.text);
	CHECK(strstr(r.text, "\xce\xb1\xce\xb2\xce\xb3") != NULL, "Symbol text: %s", r.text);
	CHECK(has_ink(&r, 40, 755, 200, 780), "Helvetica glyphs not drawn");
	CHECK(has_ink(&r, 40, 665, 100, 690), "Symbol glyphs not drawn");
	CHECK(has_ink(&r, 40, 635, 120, 660), "ZapfDingbats glyphs not drawn");
	CHECK(pixel_is(&r, 140, 530, 0, 0, 255), "blue rectangle missing");
	done(&r);

	run(dir, "jpx.pdf", &r);
	CHECK(r.w.flags == TCL_WARN_JPX, "jpx.pdf flags=%u", r.w.flags);
	CHECK(strstr(r.text, "JPEG 2000") != NULL, "text: %s", r.text);
	done(&r);

	run(dir, "annot.pdf", &r);
	CHECK(r.w.flags == TCL_WARN_ANNOT, "annot.pdf flags=%u", r.w.flags);
	CHECK(pixel_is(&r, 150, 550, 0, 153, 0), "annotation appearance not drawn");
	CHECK(pixel_is(&r, 350, 550, 255, 255, 255), "annotation without appearance was drawn");
	done(&r);

	run(dir, "cjk.pdf", &r);
	CHECK(r.w.flags & TCL_WARN_CMAP, "cjk.pdf flags=%u", r.w.flags);
	CHECK(!strcmp(r.w.cmap, "UniJIS-UCS2-H"), "cmap=%s", r.w.cmap);
	CHECK(strstr(r.text, "CJK test") != NULL, "text: %s", r.text);
	done(&r);

	tcl_fonts_cleanup();
	printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
	return failures ? 1 : 0;
}
