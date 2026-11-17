# tcl-mupdfv: specification

A minimal, fast PDF viewer for Total Commander's Lister (F3) and Quick View (Ctrl+Q), built on a trimmed MuPDF.

## Scope

| Feature | Behaviour |
|---|---|
| Rendering | All pages stacked vertically. On open, one whole page (the largest) fits the view: a one-page file is shown completely, a longer one page by page. |
| Scrolling | Mouse wheel, scroll bars, arrows, PgUp/PgDn, Space, Home/End. Shift+wheel scrolls horizontally. |
| Zoom | Ctrl+mouse wheel, ×1.15 per notch, kept around the cursor. Saved only with `remember_zoom=1` (see Settings). |
| Selection | Drag with the left mouse button, across pages too. Ctrl+A or Lister's *Select all* selects everything. A click clears the selection. |
| Copy | Ctrl+C, Ctrl+Insert or Lister's *Copy*: plain Unicode text with CRLF line ends. |
| Warnings | A one-line strip at the top lists content the trimmed build skips (see below). Click it to hide it. |

Not included: search, printing, thumbnails, links, outline, forms, password-protected files, XPS. A file that can't be opened makes `ListLoad` return `NULL`, so Total Commander falls back to its next viewer.

## Settings

`tcl-mupdfv.ini` next to the DLL (optional; read on every load):

| Key (`[General]`) | Default | Meaning |
|---|---|---|
| `remember_zoom` | `0` | `1`: open every file at the last Ctrl+wheel zoom (global, not per file). `0`: always fit one page. |
| `zoom` | `0` | Last zoom in percent (100 = actual size), `0` = fit page. Written by the plugin when a file is closed or replaced, only if `remember_zoom=1`. |

## Plugin interface

- **Exports:** `ListLoad[W]`, `ListLoadNext[W]`, `ListCloseWindow`, `ListGetDetectString` (`EXT="PDF"`), `ListSendCommand` (`lc_copy`, `lc_selectall`).
- **Builds:** `tcl-mupdfv.wlx64` (x64) and `tcl-mupdfv.wlx` (x86), with static CRT; the INI is optional.
- **Keys:** the viewer is the plugin window itself. Lister subclasses it, so Lister's hotkeys keep working.
- **Focus:** the viewer takes focus only when it is clicked (so Quick View doesn't steal focus).
- **State:** everything is per window, apart from the read-only Windows font mappings shared between windows and the saved zoom (last closed window wins).

## Trimmed MuPDF

MuPDF 1.28.6 is a submodule in `extern/mupdf`, unmodified. `cmake/mupdf.cmake` builds `fitz` and `pdf` plus FreeType, libjpeg, jbig2dec and zlib. Everything else is switched off with MuPDF's `FZ_ENABLE_*` options: XPS, SVG, CBZ, images, HTML/EPUB/FB2/MOBI/office, JavaScript, colour management, Brotli, barcodes, hyphenation, output formats, spot rendering and the CMYK plotters. All bundled fonts are excluded (`TOFU`, `TOFU_CJK`, `TOFU_BASE14`).

The replacements live in `mupdf-overrides/`:

| File | Replaces | What it does |
|---|---|---|
| `tcl_fonts.c` | `fz_lookup_base14_font`, `fz_lookup_builtin_font` (in `noto.c`, renamed away at compile time) | Helvetica, Times and Courier come from Windows Arial, Times New Roman and Courier New, memory-mapped on first use. Symbol and ZapfDingbats stay built in (40 KB). Non-embedded CJK fonts use Windows CJK fonts (YaHei/SimSun, JhengHei/MingLiU, Yu Gothic/MS Gothic/MS Mincho, Malgun/Batang). |
| `tcl_cmap.c` | `pdf-cmap-load.c` | Only the 4 Adobe-*-UCS2 CMaps (text extraction for CJK fonts) instead of all 70. |
| `tcl_stubs.c` | `load-jpx.c`, `pdf-appearance.c`, `load-tiff.c`, `load-bmp.c`, `load-pnm.c` | JPEG 2000 images are skipped. Annotations and form fields without an appearance stream are not drawn; those with one draw normally. TIFF/BMP/PNM loaders are never used by PDF. |

The rasterizer, JPEG and Flate decoders and glyph rendering are compiled for speed (`/O2`, `-O2`); everything else for size (`/O1`, `-Os`).

### Warnings

The stand-ins report what they skip through `tcl_warn()` into a per-document `tcl_warnings` struct, attached with `fz_set_user_context`. The viewer shows them as they come up while pages render:

| Flag | Shown as |
|---|---|
| `TCL_WARN_JPX` | JPEG 2000 images are not shown |
| `TCL_WARN_ANNOT` | Annotations or form fields without an appearance are not shown |
| `TCL_WARN_CMAP` | CJK encoding *name* is not built in, some text may be missing |
| `TCL_WARN_FONT` | Font *name* is not available, some text may be missing |

If CJK documents show `TCL_WARN_CMAP` often, the next step is to ship the remaining CMaps as an optional data file next to the plugin.

## Rendering

- **Layout:** page sizes come from the page tree without loading the pages.
- **Rendering:** a page is loaded once into a display list (24 pages are cached). The visible part, plus half a screen above and below, is rendered straight into a 24-bit DIB section, and up to 4 such tiles are kept.
- **Zoom:** a zoom change re-renders the visible tiles.
- **Painting:** double-buffered.
- **Selection:** uses MuPDF's structured text (`fz_highlight_selection`, `fz_copy_selection`). Highlights are drawn with `R2_MASKPEN`, so text stays readable.
- **Errors:** every MuPDF call is in `src/Document.cpp`, inside `fz_try`, with no C++ objects that need destructors inside those blocks.

## Tests

- **`core_test`** (Windows, Linux): renders `tests/samples/*.pdf` with the trimmed MuPDF and checks pixels, copied text and warning flags. On Linux, MuPDF's URW fonts stand in for the Windows fonts (`TCL_MUPDFV_FONT_DIR`).
- **`plugin_smoke`** (Windows): loads the real DLL in a hidden `TLister` window and checks loading, painting, select all, drag selection, copy, Ctrl+wheel zoom, `remember_zoom`, wheel scrolling, the warning strip, `ListLoadNext` and `ListCloseWindow`. With `TCL_SMOKE_DUMP=<dir>` it also saves its screenshots as BMP files.
- **Samples:** `tests/make_samples.py` regenerates the PDFs.

CI runs both tests with MSVC (x64 and x86), the core test on Linux, and a MinGW cross-compile. MinGW DLLs are larger (about 3.1 MB) because GNU ld keeps functions alive through their unwind entries on PE; release builds use MSVC.

## Licence

MuPDF is AGPL-3.0, so this plugin is AGPL-3.0 too (see `LICENSE`).
