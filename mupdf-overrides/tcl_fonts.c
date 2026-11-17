// Standard PDF fonts from Windows instead of MuPDF's built-in URW fonts.
//
// Replaces fz_lookup_base14_font() and fz_lookup_builtin_font() from
// source/fitz/noto.c (which is compiled with these two functions renamed away).
// Helvetica, Times and Courier map to Arial, Times New Roman and Courier New,
// which are metric-compatible. Symbol and ZapfDingbats stay built in: Windows
// has no ZapfDingbats, and its Symbol font uses a different encoding.
//
// The font files are memory-mapped on first use and stay mapped until
// tcl_fonts_cleanup(). All calls happen on the UI thread, so no locking.
#include "tcl_mupdf.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdio.h>
#endif

extern const unsigned char tcl_font_dingbats[];
extern const int tcl_font_dingbats_size;
extern const unsigned char tcl_font_symbol[];
extern const int tcl_font_symbol_size;

typedef struct {
	const char *file;  // file name in the Windows Fonts folder
	const unsigned char *data;
	size_t size;
	int tried;
} font_slot;

static font_slot slots[] = {
	// 0-11: standard fonts, in base14 order below
	{"cour.ttf"}, {"couri.ttf"}, {"courbd.ttf"}, {"courbi.ttf"},
	{"arial.ttf"}, {"ariali.ttf"}, {"arialbd.ttf"}, {"arialbi.ttf"},
	{"times.ttf"}, {"timesi.ttf"}, {"timesbd.ttf"}, {"timesbi.ttf"},
	// 12-: CJK fallbacks for fonts that are not embedded
	{"msyh.ttc"}, {"simsun.ttc"},                 // 12, 13: Chinese (simplified)
	{"msjh.ttc"}, {"mingliu.ttc"},                // 14, 15: Chinese (traditional)
	{"YuGothM.ttc"}, {"msgothic.ttc"}, {"msmincho.ttc"}, // 16-18: Japanese
	{"malgun.ttf"}, {"batang.ttc"},               // 19, 20: Korean
};

static const char *base14[] = {
	"Courier", "Courier-Oblique", "Courier-Bold", "Courier-BoldOblique",
	"Helvetica", "Helvetica-Oblique", "Helvetica-Bold", "Helvetica-BoldOblique",
	"Times-Roman", "Times-Italic", "Times-Bold", "Times-BoldItalic",
};

#ifdef _WIN32
static const unsigned char *map_font_file(const char *name, size_t *size)
{
	WCHAR path[MAX_PATH + 64];
	UINT n = GetWindowsDirectoryW(path, MAX_PATH);
	HANDLE file, mapping;
	LARGE_INTEGER len;
	void *view = NULL;
	if (n == 0 || n >= MAX_PATH)
		return NULL;
	wcscpy(path + n, L"\\Fonts\\");
	n += 7;
	while (*name)
		path[n++] = (WCHAR)(unsigned char)*name++;
	path[n] = 0;

	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return NULL;
	if (GetFileSizeEx(file, &len) && len.QuadPart > 0 && len.QuadPart < 0x7fffffff)
	{
		mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
		if (mapping)
		{
			view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
			CloseHandle(mapping); // the view keeps the mapping alive
		}
	}
	CloseHandle(file);
	if (view)
		*size = (size_t)len.QuadPart;
	return view;
}

static void unmap_font_file(const unsigned char *data)
{
	UnmapViewOfFile((void *)data);
}
#else
// Non-Windows builds (tests): read <TCL_MUPDFV_FONT_DIR>/<name> into memory.
static const unsigned char *map_font_file(const char *name, size_t *size)
{
	const char *dir = getenv("TCL_MUPDFV_FONT_DIR");
	char path[1024];
	FILE *f;
	unsigned char *buf = NULL;
	long len;
	if (!dir || strlen(dir) + strlen(name) + 2 > sizeof path)
		return NULL;
	strcpy(path, dir);
	strcat(path, "/");
	strcat(path, name);
	f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0)
	{
		buf = malloc((size_t)len);
		if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len)
		{
			free(buf);
			buf = NULL;
		}
		if (buf)
			*size = (size_t)len;
	}
	fclose(f);
	return buf;
}

static void unmap_font_file(const unsigned char *data)
{
	free((void *)data);
}
#endif

static const unsigned char *slot_data(int i, size_t *size)
{
	font_slot *s = &slots[i];
	if (!s->tried)
	{
		s->tried = 1;
		s->data = map_font_file(s->file, &s->size);
	}
	*size = s->size;
	return s->data;
}

void tcl_fonts_cleanup(void)
{
	size_t i;
	for (i = 0; i < nelem(slots); i++)
	{
		if (slots[i].data)
			unmap_font_file(slots[i].data);
		slots[i].data = NULL;
		slots[i].size = 0;
		slots[i].tried = 0;
	}
}

const unsigned char *
fz_lookup_base14_font(fz_context *ctx, const char *name, int *size)
{
	size_t i, len;
	*size = 0;
	if (!strcmp(name, "Symbol"))
		return *size = tcl_font_symbol_size, tcl_font_symbol;
	if (!strcmp(name, "ZapfDingbats"))
		return *size = tcl_font_dingbats_size, tcl_font_dingbats;
	for (i = 0; i < nelem(base14); i++)
	{
		if (!strcmp(name, base14[i]))
		{
			const unsigned char *data = slot_data((int)i, &len);
			if (!data)
			{
				tcl_warn(ctx, TCL_WARN_FONT, slots[i].file);
				return NULL;
			}
			*size = (int)len;
			return data;
		}
	}
	return NULL;
}

const unsigned char *
fz_lookup_builtin_font(fz_context *ctx, const char *family, int is_bold, int is_italic, int *size)
{
	int style = (is_bold ? 2 : 0) + (is_italic ? 1 : 0);
	int base;
	if (!strcmp(family, "Courier"))
		base = 0;
	else if (!strcmp(family, "Helvetica") || !strcmp(family, "Arial"))
		base = 4;
	else if (!strcmp(family, "Times") || !strcmp(family, "Times-Roman") || !strcmp(family, "Times New Roman"))
		base = 8;
	else if (!strcmp(family, "Symbol") || !strcmp(family, "ZapfDingbats"))
		return fz_lookup_base14_font(ctx, family, size);
	else
		return *size = 0, NULL;
	return fz_lookup_base14_font(ctx, base14[base + style], size);
}

// CJK text whose font is not embedded: use a Windows CJK font.
static fz_font *load_cjk_font(fz_context *ctx, const char *name, int ordering, int serif)
{
	static const int candidates[4][3] = {
		/* FZ_ADOBE_CNS */ {14, 15, -1},
		/* FZ_ADOBE_GB */ {12, 13, -1},
		/* FZ_ADOBE_JAPAN */ {16, 17, 18},
		/* FZ_ADOBE_KOREA */ {19, 20, -1},
	};
	int k;
	(void)serif;
	if (ordering < 0 || ordering > 3)
		return NULL;
	for (k = 0; k < 3 && candidates[ordering][k] >= 0; k++)
	{
		size_t len;
		const unsigned char *data = slot_data(candidates[ordering][k], &len);
		if (data)
			return fz_new_font_from_memory(ctx, name, data, (int)len, 0, 0);
	}
	tcl_warn(ctx, TCL_WARN_FONT, name);
	return NULL;
}

void tcl_install_fonts(fz_context *ctx)
{
	fz_install_load_system_font_funcs(ctx, NULL, load_cjk_font, NULL);
}
