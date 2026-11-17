// Copyright (C) 2004-2024 Artifex Software, Inc.
//
// This file is part of MuPDF.
//
// MuPDF is free software: you can redistribute it and/or modify it under the
// terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// MuPDF is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
// details.
//
// You should have received a copy of the GNU Affero General Public License
// along with MuPDF. If not, see <https://www.gnu.org/licenses/agpl-3.0.en.html>
//
// Alternative licensing terms are available from the licensor.
// For commercial licensing, see <https://www.artifex.com/> or contact
// Artifex Software, Inc., 39 Mesa Street, Suite 108A, San Francisco,
// CA 94129, USA, for further information.


// tcl-mupdfv: copy of MuPDF 1.28.6 source/pdf/pdf-cmap-load.c.
// Changes: only the four Adobe-*-UCS2 CMaps (used for text extraction of CJK
// fonts) are built in, instead of all 70; a request for any other CMap is
// recorded with tcl_warn(TCL_WARN_CMAP) so the viewer can say so.
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include "cmaps/TrueType-UCS2.h"
#include "cmaps/Adobe-CNS1-UCS2.h"
#include "cmaps/Adobe-GB1-UCS2.h"
#include "cmaps/Adobe-Japan1-UCS2.h"
#include "cmaps/Adobe-Korea1-UCS2.h"

#include "tcl_mupdf.h"

#include <string.h>

static pdf_cmap *
pdf_load_embedded_cmap_imp(fz_context *ctx, pdf_document *doc, pdf_obj *stmobj, pdf_cycle_list *cycle_up)
{
	pdf_cycle_list cycle;
	fz_stream *file = NULL;
	pdf_cmap *cmap = NULL;
	pdf_cmap *usecmap = NULL;
	pdf_obj *obj;

	fz_var(file);
	fz_var(cmap);
	fz_var(usecmap);

	if ((cmap = pdf_find_item(ctx, pdf_drop_cmap_imp, stmobj)) != NULL)
		return cmap;

	fz_try(ctx)
	{
		file = pdf_open_stream(ctx, stmobj);
		cmap = pdf_load_cmap(ctx, file);

		obj = pdf_dict_get(ctx, stmobj, PDF_NAME(WMode));
		if (pdf_is_int(ctx, obj))
			pdf_set_cmap_wmode(ctx, cmap, pdf_to_int(ctx, obj));

		obj = pdf_dict_get(ctx, stmobj, PDF_NAME(UseCMap));
		if (pdf_is_name(ctx, obj))
		{
			fz_try(ctx)
			{
				usecmap = pdf_load_system_cmap(ctx, pdf_to_name(ctx, obj));
				pdf_set_usecmap(ctx, cmap, usecmap);
			}
			fz_catch(ctx)
			{
				fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
				fz_report_error(ctx);
				fz_warn(ctx, "cannot load system CMap: %s", pdf_to_name(ctx, obj));
			}
		}
		else if (pdf_is_indirect(ctx, obj))
		{
			if (pdf_cycle(ctx, &cycle, cycle_up, obj))
				fz_throw(ctx, FZ_ERROR_FORMAT, "recursive CMap");
			usecmap = pdf_load_embedded_cmap_imp(ctx, doc, obj, &cycle);
			pdf_set_usecmap(ctx, cmap, usecmap);
		}
		else if (strlen(cmap->usecmap_name) > 0)
		{
			fz_try(ctx)
			{
				usecmap = pdf_load_system_cmap(ctx, cmap->usecmap_name);
				pdf_set_usecmap(ctx, cmap, usecmap);
			}
			fz_catch(ctx)
			{
				fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
				fz_report_error(ctx);
				fz_warn(ctx, "cannot load system CMap: %s", pdf_to_name(ctx, obj));
			}
		}

		pdf_store_item(ctx, stmobj, cmap, pdf_cmap_size(ctx, cmap));
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, file);
		pdf_drop_cmap(ctx, usecmap);
	}
	fz_catch(ctx)
	{
		pdf_drop_cmap(ctx, cmap);
		fz_rethrow(ctx);
	}

	return cmap;
}

pdf_cmap *
pdf_load_embedded_cmap(fz_context *ctx, pdf_document *doc, pdf_obj *stmobj)
{
	return pdf_load_embedded_cmap_imp(ctx, doc, stmobj, NULL);
}

pdf_cmap *
pdf_new_identity_cmap(fz_context *ctx, int wmode, int bytes)
{
	pdf_cmap *cmap = pdf_new_cmap(ctx);
	fz_try(ctx)
	{
		unsigned int high = (1 << (bytes * 8)) - 1;
		if (wmode)
			fz_strlcpy(cmap->cmap_name, "Identity-V", sizeof cmap->cmap_name);
		else
			fz_strlcpy(cmap->cmap_name, "Identity-H", sizeof cmap->cmap_name);
		pdf_add_codespace(ctx, cmap, 0, high, bytes);
		pdf_map_range_to_range(ctx, cmap, 0, high, 0);
		pdf_sort_cmap(ctx, cmap);
		pdf_set_cmap_wmode(ctx, cmap, wmode);
	}
	fz_catch(ctx)
	{
		pdf_drop_cmap(ctx, cmap);
		fz_rethrow(ctx);
	}
	return cmap;
}

pdf_cmap *
pdf_load_builtin_cmap(fz_context *ctx, const char *name)
{
	if (!strcmp(name, "Identity-H")) return pdf_new_identity_cmap(ctx, 0, 2);
	if (!strcmp(name, "Identity-V")) return pdf_new_identity_cmap(ctx, 1, 2);
	if (!strcmp(name, "TrueType-UCS2")) return &cmap_TrueType_UCS2;
	if (!strcmp(name, "Adobe-CNS1-UCS2")) return &cmap_Adobe_CNS1_UCS2;
	if (!strcmp(name, "Adobe-GB1-UCS2")) return &cmap_Adobe_GB1_UCS2;
	if (!strcmp(name, "Adobe-Japan1-UCS2")) return &cmap_Adobe_Japan1_UCS2;
	if (!strcmp(name, "Adobe-Korea1-UCS2")) return &cmap_Adobe_Korea1_UCS2;
	tcl_warn(ctx, TCL_WARN_CMAP, name);
	return NULL;
}

pdf_cmap *
pdf_load_system_cmap(fz_context *ctx, const char *cmap_name)
{
	pdf_cmap *usecmap;
	pdf_cmap *cmap;

	cmap = pdf_load_builtin_cmap(ctx, cmap_name);
	if (!cmap)
		fz_throw(ctx, FZ_ERROR_FORMAT, "no builtin cmap file: %s", cmap_name);

	if (cmap->usecmap_name[0] && !cmap->usecmap)
	{
		usecmap = pdf_load_system_cmap(ctx, cmap->usecmap_name);
		if (!usecmap)
			fz_throw(ctx, FZ_ERROR_FORMAT, "no builtin cmap file: %s", cmap->usecmap_name);
		pdf_set_usecmap(ctx, cmap, usecmap);
	}

	return cmap;
}
