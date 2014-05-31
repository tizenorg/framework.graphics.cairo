/*
 * Copyright © 2012 SCore Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * Author: Taekyun Kim (podain77@gmail.com)
 */

#ifndef CAIRO_TG_JOURNAL_PRIVATE_H
#define CAIRO_TG_JOURNAL_PRIVATE_H

#include "cairoint.h"
#include "cairo-pattern-private.h"
#include "cairo-clip-private.h"
#include "cairo-surface-private.h"
#include "cairo-list-private.h"
#include "cairo-list-inline.h"
#include "cairo-tg-allocator-private.h"
#include "cairo-mutex-private.h"

typedef enum
{
    CAIRO_TG_JOURNAL_ENTRY_PAINT,
    CAIRO_TG_JOURNAL_ENTRY_MASK,
    CAIRO_TG_JOURNAL_ENTRY_FILL,
    CAIRO_TG_JOURNAL_ENTRY_STROKE,
    CAIRO_TG_JOURNAL_ENTRY_GLYPHS,
} cairo_tg_journal_entry_type_t;

typedef struct _cairo_tg_journal_entry cairo_tg_journal_entry_t;

struct _cairo_tg_journal_entry
{
    cairo_list_t		    link;
    cairo_tg_journal_entry_type_t   type;

    cairo_rectangle_int_t	    extents;

    cairo_operator_t		    op;
    cairo_pattern_union_t	    source;
    cairo_clip_t		    *clip;
};

typedef struct _cairo_tg_journal_entry_paint
{
    cairo_tg_journal_entry_t base;
} cairo_tg_journal_entry_paint_t;

typedef struct _cairo_tg_journal_entry_mask
{
    cairo_tg_journal_entry_t	base;

    cairo_pattern_union_t	mask;
} cairo_tg_journal_entry_mask_t;

typedef struct _cairo_tg_journal_entry_stroke
{
    cairo_tg_journal_entry_t	base;

    cairo_path_fixed_t		path;
    cairo_stroke_style_t	style;
    cairo_matrix_t		ctm;
    cairo_matrix_t		ctm_inverse;
    double			tolerance;
    cairo_antialias_t		antialias;
} cairo_tg_journal_entry_stroke_t;

typedef struct _cairo_tg_journal_entry_fill
{
    cairo_tg_journal_entry_t	base;

    cairo_path_fixed_t		path;
    cairo_fill_rule_t		fill_rule;
    double			tolerance;
    cairo_antialias_t		antialias;
} cairo_tg_journal_entry_fill_t;

typedef struct _cairo_tg_journal_entry_glyphs
{
    cairo_tg_journal_entry_t	base;

    cairo_glyph_t		*glyphs;
    int				num_glyphs;
    cairo_scaled_font_t		*scaled_font;
} cairo_tg_journal_entry_glyphs_t;

typedef struct _cairo_tg_journal
{
    cairo_rectangle_int_t	extents;
    cairo_list_t		entry_list;
    int				num_entries;
    cairo_tg_mono_allocator_t	allocator;
    cairo_mutex_t		mutex;
} cairo_tg_journal_t;

typedef struct _cairo_tg_journal_replay_funcs
{
    cairo_int_status_t
    (*paint)	(void			    *closure,
		 cairo_operator_t	    op,
		 const cairo_pattern_t	    *source,
		 const cairo_clip_t	    *clip);

    cairo_int_status_t
    (*mask)	(void			    *closure,
		 cairo_operator_t	    op,
		 const cairo_pattern_t	    *source,
		 const cairo_pattern_t	    *mask,
		 const cairo_clip_t	    *clip);

    cairo_int_status_t
    (*stroke)	(void			    *closure,
		 cairo_operator_t	    op,
		 const cairo_pattern_t	    *source,
		 const cairo_path_fixed_t   *path,
		 const cairo_stroke_style_t *style,
		 const cairo_matrix_t	    *ctm,
		 const cairo_matrix_t	    *ctm_inverse,
		 double			    tolerance,
		 cairo_antialias_t	    antialias,
		 const cairo_clip_t	    *clip);

    cairo_int_status_t
    (*fill)	(void			    *closure,
		 cairo_operator_t	    op,
		 const cairo_pattern_t	    *source,
		 const cairo_path_fixed_t   *path,
		 cairo_fill_rule_t	    fill_rule,
		 double			    tolerance,
		 cairo_antialias_t	    antialias,
		 const cairo_clip_t	    *clip);

    cairo_int_status_t
    (*glyphs)	(void			    *closure,
		 cairo_operator_t	    op,
		 const cairo_pattern_t	    *source,
		 cairo_glyph_t		    *glyphs,
		 int			    num_glyphs,
		 cairo_scaled_font_t	    *scaled_font,
		 const cairo_clip_t	    *clip);
} cairo_tg_journal_replay_funcs_t;

cairo_int_status_t
_cairo_tg_journal_init (cairo_tg_journal_t *journal);

void
_cairo_tg_journal_fini (cairo_tg_journal_t *journal);

void
_cairo_tg_journal_lock (cairo_tg_journal_t *journal);

void
_cairo_tg_journal_unlock (cairo_tg_journal_t *journal);

cairo_int_status_t
_cairo_tg_journal_log_paint (cairo_tg_journal_t	    *journal,
			     cairo_operator_t	    op,
			     const cairo_pattern_t  *source,
			     const cairo_clip_t	    *clip);

cairo_int_status_t
_cairo_tg_journal_log_mask (cairo_tg_journal_t	    *journal,
			    cairo_operator_t	    op,
			    const cairo_pattern_t   *source,
			    const cairo_pattern_t   *mask,
			    const cairo_clip_t	    *clip);

cairo_int_status_t
_cairo_tg_journal_log_stroke (cairo_tg_journal_t	    *journal,
			      cairo_operator_t		    op,
			      const cairo_pattern_t	    *source,
			      const cairo_path_fixed_t	    *path,
			      const cairo_stroke_style_t    *style,
			      const cairo_matrix_t	    *ctm,
			      const cairo_matrix_t	    *ctm_inverse,
			      double			    tolerance,
			      cairo_antialias_t		    antialias,
			      const cairo_clip_t	    *clip);

cairo_int_status_t
_cairo_tg_journal_log_fill (cairo_tg_journal_t		*journal,
			    cairo_operator_t		op,
			    const cairo_pattern_t	*source,
			    const cairo_path_fixed_t	*path,
			    cairo_fill_rule_t		fill_rule,
			    double			tolerance,
			    cairo_antialias_t		antialias,
			    const cairo_clip_t		*clip);

cairo_int_status_t
_cairo_tg_journal_log_glyphs (cairo_tg_journal_t	*journal,
			      cairo_operator_t		op,
			      const cairo_pattern_t	*source,
			      cairo_glyph_t		*glyphs,
			      int			num_glyphs,
			      cairo_scaled_font_t	*scaled_font,
			      const cairo_clip_t	*clip);

void
_cairo_tg_journal_clear (cairo_tg_journal_t *journal);

cairo_int_status_t
_cairo_tg_journal_replay (const cairo_tg_journal_t		*journal,
			  void					*closure,
			  const cairo_rectangle_int_t		*extents,
			  const cairo_tg_journal_replay_funcs_t	*funcs);

#endif /* CAIRO_TG_JOURNAL_PRIVATE_H */
