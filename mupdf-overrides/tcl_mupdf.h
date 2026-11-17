// Glue between the trimmed MuPDF build and the plugin.
//
// The replacement files in this folder (fonts, CMaps, stubs for JPEG 2000,
// annotation appearances and image loaders) report features they could not
// handle through tcl_warn(). The plugin attaches a tcl_warnings struct to each
// fz_context with fz_set_user_context() and shows the collected flags.
#ifndef TCL_MUPDF_H
#define TCL_MUPDF_H

#include "mupdf/fitz.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TCL_WARN_JPX = 1,    // a JPEG 2000 image was skipped
	TCL_WARN_ANNOT = 2,  // an annotation or form field has no appearance stream
	TCL_WARN_CMAP = 4,   // a CJK CMap that is not built in was requested
	TCL_WARN_FONT = 8,   // a Windows font file needed for a standard font is missing
};

typedef struct {
	unsigned flags;
	char cmap[64];  // first missing CMap name
	char font[64];  // first missing font name
} tcl_warnings;

// Records a warning in the tcl_warnings attached to ctx (no-op if none).
void tcl_warn(fz_context *ctx, unsigned flag, const char *detail);

// Installs the Windows CJK font loader. Call once per context.
void tcl_install_fonts(fz_context *ctx);

// Unmaps the Windows font files. Call only when no fz_context is alive.
void tcl_fonts_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
