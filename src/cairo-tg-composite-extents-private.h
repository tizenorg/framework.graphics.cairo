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

#ifndef CAIRO_TG_COMPOSITE_EXTENTS_PRIVATE_H
#define CAIRO_TG_COMPOSITE_EXTENTS_PRIVATE_H

#include "cairoint.h"
#include "cairo-pattern-private.h"
#include "cairo-clip-private.h"
#include "cairo-surface-private.h"

static inline void
_cairo_tg_approximate_paint_extents (cairo_rectangle_int_t  *extents,
				     cairo_operator_t	    op,
				     const cairo_pattern_t  *source,
				     const cairo_clip_t	    *clip)
{
    *extents = * (_cairo_clip_get_extents (clip));
}

static inline void
_cairo_tg_approximate_mask_extents (cairo_rectangle_int_t   *extents,
				    cairo_operator_t	    op,
				    const cairo_pattern_t   *source,
				    const cairo_pattern_t   *mask,
				    const cairo_clip_t	    *clip)
{
    *extents = * (_cairo_clip_get_extents (clip));
}

static inline void
_cairo_tg_approximate_stroke_extents (cairo_rectangle_int_t	    *extents,
				      cairo_operator_t		    op,
				      const cairo_pattern_t	    *source,
				      const cairo_path_fixed_t	    *path,
				      const cairo_stroke_style_t    *style,
				      const cairo_matrix_t	    *ctm,
				      const cairo_matrix_t	    *ctm_inverse,
				      double			    tolerance,
				      cairo_antialias_t		    antialias,
				      const cairo_clip_t	    *clip)
{
    cairo_rectangle_int_t   rect;

    *extents = * (_cairo_clip_get_extents (clip));

    if (_cairo_operator_bounded_by_either (op))
    {
	_cairo_path_fixed_approximate_stroke_extents (path, style, ctm, &rect);
	_cairo_rectangle_intersect (extents, &rect);
    }
}

static inline void
_cairo_tg_approximate_fill_extents (cairo_rectangle_int_t	*extents,
				    cairo_operator_t		op,
				    const cairo_pattern_t	*source,
				    const cairo_path_fixed_t	*path,
				    cairo_fill_rule_t		fill_rule,
				    double			tolerance,
				    cairo_antialias_t		antialias,
				     const cairo_clip_t		*clip)
{
    cairo_rectangle_int_t   rect;

    *extents = * (_cairo_clip_get_extents (clip));

    if (_cairo_operator_bounded_by_either (op))
    {
	_cairo_path_fixed_approximate_fill_extents (path, &rect);
	_cairo_rectangle_intersect (extents, &rect);
    }
}

static inline void
_cairo_tg_approximate_glyphs_extents (cairo_rectangle_int_t *extents,
				      cairo_operator_t	    op,
				      const cairo_pattern_t *source,
				      cairo_glyph_t	    *glyphs,
				      int		    num_glyphs,
				      cairo_scaled_font_t   *scaled_font,
				     const cairo_clip_t	    *clip)
{
    cairo_rectangle_int_t   rect;

    *extents = * (_cairo_clip_get_extents (clip));

    if (_cairo_operator_bounded_by_either (op))
    {
	if (_cairo_scaled_font_glyph_approximate_extents (scaled_font, glyphs, num_glyphs, &rect))
	    _cairo_rectangle_intersect (extents, &rect);
    }
}

#endif /* CAIRO_TG_COMPOSITE_EXTENTS_PRIVATE_H */
