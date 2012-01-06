/* -*- Mode: c; tab-width: 8; c-basic-offset: 4; indent-tabs-mode: t; -*- */
/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2003 University of Southern California
 * Copyright © 2009,2010,2011 Intel Corporation
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
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is University of Southern
 * California.
 *
 * Contributor(s):
 *	Carl D. Worth <cworth@cworth.org>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 */

/* The primarily reason for keeping a traps-compositor around is
 * for validating cairo-xlib (which currently also uses traps).
 */

#include "cairoint.h"

#include "cairo-image-surface-private.h"

#include "cairo-compositor-private.h"
#include "cairo-spans-compositor-private.h"

#include "cairo-region-private.h"
#include "cairo-traps-private.h"
#include "cairo-tristrip-private.h"

static pixman_image_t *
to_pixman_image (cairo_surface_t *s)
{
    return ((cairo_image_surface_t *)s)->pixman_image;
}

static cairo_int_status_t
acquire (void *abstract_dst)
{
    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
release (void *abstract_dst)
{
    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
set_clip_region (void *_surface,
		 cairo_region_t *region)
{
    cairo_image_surface_t *surface = _surface;
    pixman_region32_t *rgn = region ? &region->rgn : NULL;

    if (! pixman_image_set_clip_region32 (surface->pixman_image, rgn))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
draw_image_boxes (void *_dst,
		  cairo_image_surface_t *image,
		  cairo_boxes_t *boxes,
		  int dx, int dy)
{
    cairo_image_surface_t *dst = _dst;
    struct _cairo_boxes_chunk *chunk;
    int i;

    for (chunk = &boxes->chunks; chunk; chunk = chunk->next) {
	for (i = 0; i < chunk->count; i++) {
	    cairo_box_t *b = &chunk->base[i];
	    int x = _cairo_fixed_integer_part (b->p1.x);
	    int y = _cairo_fixed_integer_part (b->p1.y);
	    int w = _cairo_fixed_integer_part (b->p2.x) - x;
	    int h = _cairo_fixed_integer_part (b->p2.y) - y;
	    if (dst->pixman_format != image->pixman_format ||
		! pixman_blt ((uint32_t *)image->data, (uint32_t *)dst->data,
			      image->stride / sizeof (uint32_t),
			      dst->stride / sizeof (uint32_t),
			      PIXMAN_FORMAT_BPP (image->pixman_format),
			      PIXMAN_FORMAT_BPP (dst->pixman_format),
			      x + dx, y + dy,
			      x, y,
			      w, h))
	    {
		pixman_image_composite32 (PIXMAN_OP_SRC,
					  image->pixman_image, NULL, dst->pixman_image,
					  x + dx, y + dy,
					  0, 0,
					  x, y,
					  w, h);
	    }
	}
    }
    return CAIRO_STATUS_SUCCESS;
}

static inline uint32_t
color_to_uint32 (const cairo_color_t *color)
{
    return
        (color->alpha_short >> 8 << 24) |
        (color->red_short >> 8 << 16)   |
        (color->green_short & 0xff00)   |
        (color->blue_short >> 8);
}

static inline cairo_bool_t
color_to_pixel (const cairo_color_t	*color,
                pixman_format_code_t	 format,
                uint32_t		*pixel)
{
    uint32_t c;

    if (!(format == PIXMAN_a8r8g8b8     ||
          format == PIXMAN_x8r8g8b8     ||
          format == PIXMAN_a8b8g8r8     ||
          format == PIXMAN_x8b8g8r8     ||
          format == PIXMAN_b8g8r8a8     ||
          format == PIXMAN_b8g8r8x8     ||
          format == PIXMAN_r5g6b5       ||
          format == PIXMAN_b5g6r5       ||
          format == PIXMAN_a8))
    {
	return FALSE;
    }

    c = color_to_uint32 (color);

    if (PIXMAN_FORMAT_TYPE (format) == PIXMAN_TYPE_ABGR) {
	c = ((c & 0xff000000) >>  0) |
	    ((c & 0x00ff0000) >> 16) |
	    ((c & 0x0000ff00) >>  0) |
	    ((c & 0x000000ff) << 16);
    }

    if (PIXMAN_FORMAT_TYPE (format) == PIXMAN_TYPE_BGRA) {
	c = ((c & 0xff000000) >> 24) |
	    ((c & 0x00ff0000) >>  8) |
	    ((c & 0x0000ff00) <<  8) |
	    ((c & 0x000000ff) << 24);
    }

    if (format == PIXMAN_a8) {
	c = c >> 24;
    } else if (format == PIXMAN_r5g6b5 || format == PIXMAN_b5g6r5) {
	c = ((((c) >> 3) & 0x001f) |
	     (((c) >> 5) & 0x07e0) |
	     (((c) >> 8) & 0xf800));
    }

    *pixel = c;
    return TRUE;
}

static pixman_op_t
_pixman_operator (cairo_operator_t op)
{
    switch ((int) op) {
    case CAIRO_OPERATOR_CLEAR:
	return PIXMAN_OP_CLEAR;

    case CAIRO_OPERATOR_SOURCE:
	return PIXMAN_OP_SRC;
    case CAIRO_OPERATOR_OVER:
	return PIXMAN_OP_OVER;
    case CAIRO_OPERATOR_IN:
	return PIXMAN_OP_IN;
    case CAIRO_OPERATOR_OUT:
	return PIXMAN_OP_OUT;
    case CAIRO_OPERATOR_ATOP:
	return PIXMAN_OP_ATOP;

    case CAIRO_OPERATOR_DEST:
	return PIXMAN_OP_DST;
    case CAIRO_OPERATOR_DEST_OVER:
	return PIXMAN_OP_OVER_REVERSE;
    case CAIRO_OPERATOR_DEST_IN:
	return PIXMAN_OP_IN_REVERSE;
    case CAIRO_OPERATOR_DEST_OUT:
	return PIXMAN_OP_OUT_REVERSE;
    case CAIRO_OPERATOR_DEST_ATOP:
	return PIXMAN_OP_ATOP_REVERSE;

    case CAIRO_OPERATOR_XOR:
	return PIXMAN_OP_XOR;
    case CAIRO_OPERATOR_ADD:
	return PIXMAN_OP_ADD;
    case CAIRO_OPERATOR_SATURATE:
	return PIXMAN_OP_SATURATE;

    case CAIRO_OPERATOR_MULTIPLY:
	return PIXMAN_OP_MULTIPLY;
    case CAIRO_OPERATOR_SCREEN:
	return PIXMAN_OP_SCREEN;
    case CAIRO_OPERATOR_OVERLAY:
	return PIXMAN_OP_OVERLAY;
    case CAIRO_OPERATOR_DARKEN:
	return PIXMAN_OP_DARKEN;
    case CAIRO_OPERATOR_LIGHTEN:
	return PIXMAN_OP_LIGHTEN;
    case CAIRO_OPERATOR_COLOR_DODGE:
	return PIXMAN_OP_COLOR_DODGE;
    case CAIRO_OPERATOR_COLOR_BURN:
	return PIXMAN_OP_COLOR_BURN;
    case CAIRO_OPERATOR_HARD_LIGHT:
	return PIXMAN_OP_HARD_LIGHT;
    case CAIRO_OPERATOR_SOFT_LIGHT:
	return PIXMAN_OP_SOFT_LIGHT;
    case CAIRO_OPERATOR_DIFFERENCE:
	return PIXMAN_OP_DIFFERENCE;
    case CAIRO_OPERATOR_EXCLUSION:
	return PIXMAN_OP_EXCLUSION;
    case CAIRO_OPERATOR_HSL_HUE:
	return PIXMAN_OP_HSL_HUE;
    case CAIRO_OPERATOR_HSL_SATURATION:
	return PIXMAN_OP_HSL_SATURATION;
    case CAIRO_OPERATOR_HSL_COLOR:
	return PIXMAN_OP_HSL_COLOR;
    case CAIRO_OPERATOR_HSL_LUMINOSITY:
	return PIXMAN_OP_HSL_LUMINOSITY;

    default:
	ASSERT_NOT_REACHED;
	return PIXMAN_OP_OVER;
    }
}

static cairo_bool_t
fill_reduces_to_source (cairo_operator_t op,
			const cairo_color_t *color,
			cairo_image_surface_t *dst)
{
    if (op == CAIRO_OPERATOR_SOURCE || op == CAIRO_OPERATOR_CLEAR)
	return TRUE;
    if (op == CAIRO_OPERATOR_OVER && CAIRO_COLOR_IS_OPAQUE (color))
	return TRUE;
    if (dst->base.is_clear)
	return op == CAIRO_OPERATOR_OVER || op == CAIRO_OPERATOR_ADD;

    return FALSE;
}

static cairo_int_status_t
fill_rectangles (void			*_dst,
		 cairo_operator_t	 op,
		 const cairo_color_t	*color,
		 cairo_rectangle_int_t	*rects,
		 int			 num_rects)
{
    cairo_image_surface_t *dst = _dst;
    uint32_t pixel;
    int i;

    if (fill_reduces_to_source (op, color, dst) &&
	color_to_pixel (color, dst->pixman_format, &pixel))
    {
	for (i = 0; i < num_rects; i++) {
	    pixman_fill ((uint32_t *) dst->data, dst->stride / sizeof (uint32_t),
			 PIXMAN_FORMAT_BPP (dst->pixman_format),
			 rects[i].x, rects[i].y,
			 rects[i].width, rects[i].height,
			 pixel);
	}
    }
    else
    {
	pixman_image_t *src = _pixman_image_for_color (color);

	op = _pixman_operator (op);
	for (i = 0; i < num_rects; i++) {
	    pixman_image_composite32 (op,
				      src, NULL, dst->pixman_image,
				      0, 0,
				      0, 0,
				      rects[i].x, rects[i].y,
				      rects[i].width, rects[i].height);
	}

	pixman_image_unref (src);
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
fill_boxes (void		*_dst,
	    cairo_operator_t	 op,
	    const cairo_color_t	*color,
	    cairo_boxes_t	*boxes)
{
    cairo_image_surface_t *dst = _dst;
    struct _cairo_boxes_chunk *chunk;
    uint32_t pixel;
    int i;

    if (fill_reduces_to_source (op, color, dst) &&
	color_to_pixel (color, dst->pixman_format, &pixel))
    {
	for (chunk = &boxes->chunks; chunk; chunk = chunk->next) {
	    for (i = 0; i < chunk->count; i++) {
		int x = _cairo_fixed_integer_part (chunk->base[i].p1.x);
		int y = _cairo_fixed_integer_part (chunk->base[i].p1.y);
		int w = _cairo_fixed_integer_part (chunk->base[i].p2.x) - x;
		int h = _cairo_fixed_integer_part (chunk->base[i].p2.y) - y;
		pixman_fill ((uint32_t *) dst->data,
			     dst->stride / sizeof (uint32_t),
			     PIXMAN_FORMAT_BPP (dst->pixman_format),
			     x, y, w, h, pixel);
	    }
	}
    }
    else
    {
	pixman_image_t *src = _pixman_image_for_color (color);

	op = _pixman_operator (op);
	for (chunk = &boxes->chunks; chunk; chunk = chunk->next) {
	    for (i = 0; i < chunk->count; i++) {
		int x1 = _cairo_fixed_integer_part (chunk->base[i].p1.x);
		int y1 = _cairo_fixed_integer_part (chunk->base[i].p1.y);
		int x2 = _cairo_fixed_integer_part (chunk->base[i].p2.x);
		int y2 = _cairo_fixed_integer_part (chunk->base[i].p2.y);
		pixman_image_composite32 (op,
					  src, NULL, dst->pixman_image,
					  0, 0,
					  0, 0,
					  x1, y1,
					  x2-x1, y2-y1);
	    }
	}

	pixman_image_unref (src);
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
composite (void			*_dst,
	   cairo_operator_t	op,
	   cairo_surface_t	*abstract_src,
	   cairo_surface_t	*abstract_mask,
	   int			src_x,
	   int			src_y,
	   int			mask_x,
	   int			mask_y,
	   int			dst_x,
	   int			dst_y,
	   unsigned int		width,
	   unsigned int		height)
{
    cairo_image_source_t *src = (cairo_image_source_t *)abstract_src;
    cairo_image_source_t *mask = (cairo_image_source_t *)abstract_mask;
    if (mask) {
	pixman_image_composite32 (_pixman_operator (op),
				  src->pixman_image, mask->pixman_image, to_pixman_image (_dst),
				  src_x, src_y,
				  mask_x, mask_y,
				  dst_x, dst_y,
				  width, height);
    } else {
	pixman_image_composite32 (_pixman_operator (op),
				  src->pixman_image, NULL, to_pixman_image (_dst),
				  src_x, src_y,
				  0, 0,
				  dst_x, dst_y,
				  width, height);
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
lerp (void			*_dst,
      cairo_surface_t		*abstract_src,
      cairo_surface_t		*abstract_mask,
      int			src_x,
      int			src_y,
      int			mask_x,
      int			mask_y,
      int			dst_x,
      int			dst_y,
      unsigned int		width,
      unsigned int		height)
{
    cairo_image_surface_t *dst = _dst;
    cairo_image_source_t *src = (cairo_image_source_t *)abstract_src;
    cairo_image_source_t *mask = (cairo_image_source_t *)abstract_mask;

#if PIXMAN_HAS_OP_LERP
    pixman_image_composite32 (PIXMAN_OP_LERP_SRC,
			      src->pixman_image, mask->pixman_image, dst->pixman_image,
			      src_x,  src_y,
			      mask_x, mask_y,
			      dst_x,  dst_y,
			      width,  height);
#else
    /* Punch the clip out of the destination */
    pixman_image_composite32 (PIXMAN_OP_OUT_REVERSE,
			      mask->pixman_image, NULL, dst->pixman_image,
			      mask_x, mask_y,
			      0,      0,
			      dst_x,  dst_y,
			      width,  height);

    /* Now add the two results together */
    pixman_image_composite32 (PIXMAN_OP_ADD,
			      src->pixman_image, mask->pixman_image, dst->pixman_image,
			      src_x,  src_y,
			      mask_x, mask_y,
			      dst_x,  dst_y,
			      width,  height);
#endif

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
composite_boxes (void			*_dst,
		 cairo_operator_t	op,
		 cairo_surface_t	*abstract_src,
		 cairo_surface_t	*abstract_mask,
		 int			src_x,
		 int			src_y,
		 int			mask_x,
		 int			mask_y,
		 int			dst_x,
		 int			dst_y,
		 cairo_boxes_t		*boxes,
		 const cairo_rectangle_int_t  *extents)
{
    pixman_image_t *dst = to_pixman_image (_dst);
    pixman_image_t *src = ((cairo_image_source_t *)abstract_src)->pixman_image;
    pixman_image_t *mask = abstract_mask ? ((cairo_image_source_t *)abstract_mask)->pixman_image : NULL;
    struct _cairo_boxes_chunk *chunk;
    int i;

    /* XXX consider using a region? saves multiple prepare-composite */

    if (((cairo_surface_t *)_dst)->is_clear &&
	(op == CAIRO_OPERATOR_SOURCE ||
	 op == CAIRO_OPERATOR_OVER ||
	 op == CAIRO_OPERATOR_ADD)) {
	op = PIXMAN_OP_SRC;
    } else if (mask) {
	if (op == CAIRO_OPERATOR_CLEAR) {
#if PIXMAN_HAS_OP_LERP
	    op = PIXMAN_OP_LERP_CLEAR;
#else
	    src = _pixman_image_for_color (CAIRO_COLOR_WHITE);
	    op = PIXMAN_OP_OUT_REVERSE;
#endif
	} else if (op == CAIRO_OPERATOR_SOURCE) {
#if PIXMAN_HAS_OP_LERP
	    op = PIXMAN_OP_LERP_SRC;
#else
	    return CAIRO_INT_STATUS_UNSUPPORTED;
#endif
	} else {
	    op = _pixman_operator (op);
	}
    } else {
	op = _pixman_operator (op);
    }

    for (chunk = &boxes->chunks; chunk; chunk = chunk->next) {
	for (i = 0; i < chunk->count; i++) {
	    int x1 = _cairo_fixed_integer_part (chunk->base[i].p1.x);
	    int y1 = _cairo_fixed_integer_part (chunk->base[i].p1.y);
	    int x2 = _cairo_fixed_integer_part (chunk->base[i].p2.x);
	    int y2 = _cairo_fixed_integer_part (chunk->base[i].p2.y);

	    pixman_image_composite32 (op, src, mask, dst,
				      x1 + src_x, y1 + src_y,
				      x1 + mask_x, y1 + mask_y,
				      x1 + dst_x, y1 + dst_y,
				      x2 - x1, y2 - y1);
	}
    }

    return CAIRO_STATUS_SUCCESS;
}

#define CAIRO_FIXED_16_16_MIN _cairo_fixed_from_int (-32768)
#define CAIRO_FIXED_16_16_MAX _cairo_fixed_from_int (32767)

static cairo_bool_t
line_exceeds_16_16 (const cairo_line_t *line)
{
    return
	line->p1.x <= CAIRO_FIXED_16_16_MIN ||
	line->p1.x >= CAIRO_FIXED_16_16_MAX ||

	line->p2.x <= CAIRO_FIXED_16_16_MIN ||
	line->p2.x >= CAIRO_FIXED_16_16_MAX ||

	line->p1.y <= CAIRO_FIXED_16_16_MIN ||
	line->p1.y >= CAIRO_FIXED_16_16_MAX ||

	line->p2.y <= CAIRO_FIXED_16_16_MIN ||
	line->p2.y >= CAIRO_FIXED_16_16_MAX;
}

static void
project_line_x_onto_16_16 (const cairo_line_t *line,
			   cairo_fixed_t top,
			   cairo_fixed_t bottom,
			   pixman_line_fixed_t *out)
{
    /* XXX use fixed-point arithmetic? */
    cairo_point_double_t p1, p2;
    double m;

    p1.x = _cairo_fixed_to_double (line->p1.x);
    p1.y = _cairo_fixed_to_double (line->p1.y);

    p2.x = _cairo_fixed_to_double (line->p2.x);
    p2.y = _cairo_fixed_to_double (line->p2.y);

    m = (p2.x - p1.x) / (p2.y - p1.y);
    out->p1.x = _cairo_fixed_16_16_from_double (p1.x + m * _cairo_fixed_to_double (top - line->p1.y));
    out->p2.x = _cairo_fixed_16_16_from_double (p1.x + m * _cairo_fixed_to_double (bottom - line->p1.y));
}

void
_pixman_image_add_traps (pixman_image_t *image,
			 int dst_x, int dst_y,
			 cairo_traps_t *traps)
{
    cairo_trapezoid_t *t = traps->traps;
    int num_traps = traps->num_traps;
    while (num_traps--) {
	pixman_trapezoid_t trap;

	/* top/bottom will be clamped to surface bounds */
	trap.top = _cairo_fixed_to_16_16 (t->top);
	trap.bottom = _cairo_fixed_to_16_16 (t->bottom);

	/* However, all the other coordinates will have been left untouched so
	 * as not to introduce numerical error. Recompute them if they
	 * exceed the 16.16 limits.
	 */
	if (unlikely (line_exceeds_16_16 (&t->left))) {
	    project_line_x_onto_16_16 (&t->left, t->top, t->bottom, &trap.left);
	    trap.left.p1.y = trap.top;
	    trap.left.p2.y = trap.bottom;
	} else {
	    trap.left.p1.x = _cairo_fixed_to_16_16 (t->left.p1.x);
	    trap.left.p1.y = _cairo_fixed_to_16_16 (t->left.p1.y);
	    trap.left.p2.x = _cairo_fixed_to_16_16 (t->left.p2.x);
	    trap.left.p2.y = _cairo_fixed_to_16_16 (t->left.p2.y);
	}

	if (unlikely (line_exceeds_16_16 (&t->right))) {
	    project_line_x_onto_16_16 (&t->right, t->top, t->bottom, &trap.right);
	    trap.right.p1.y = trap.top;
	    trap.right.p2.y = trap.bottom;
	} else {
	    trap.right.p1.x = _cairo_fixed_to_16_16 (t->right.p1.x);
	    trap.right.p1.y = _cairo_fixed_to_16_16 (t->right.p1.y);
	    trap.right.p2.x = _cairo_fixed_to_16_16 (t->right.p2.x);
	    trap.right.p2.y = _cairo_fixed_to_16_16 (t->right.p2.y);
	}

	pixman_rasterize_trapezoid (image, &trap, -dst_x, -dst_y);
	t++;
    }
}

static cairo_int_status_t
composite_traps (void			*_dst,
		 cairo_operator_t	op,
		 cairo_surface_t	*abstract_src,
		 int			src_x,
		 int			src_y,
		 int			dst_x,
		 int			dst_y,
		 const cairo_rectangle_int_t *extents,
		 cairo_antialias_t	antialias,
		 cairo_traps_t		*traps)
{
    cairo_image_surface_t *dst = (cairo_image_surface_t *) _dst;
    cairo_image_source_t *src = (cairo_image_source_t *) abstract_src;
    pixman_image_t *mask;
    pixman_format_code_t format;

    /* Special case adding trapezoids onto a mask surface; we want to avoid
     * creating an intermediate temporary mask unnecessarily.
     *
     * We make the assumption here that the portion of the trapezoids
     * contained within the surface is bounded by [dst_x,dst_y,width,height];
     * the Cairo core code passes bounds based on the trapezoid extents.
     */
    format = antialias == CAIRO_ANTIALIAS_NONE ? PIXMAN_a1 : PIXMAN_a8;
    if (dst->pixman_format == format &&
	(abstract_src == NULL ||
	 (op == CAIRO_OPERATOR_ADD && src->is_opaque_solid)))
    {
	_pixman_image_add_traps (dst->pixman_image, dst_x, dst_y, traps);
	return CAIRO_STATUS_SUCCESS;
    }

    mask = pixman_image_create_bits (format,
				     extents->width, extents->height,
				     NULL, 0);
    if (unlikely (mask == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    _pixman_image_add_traps (mask, extents->x, extents->y, traps);
    pixman_image_composite32 (_pixman_operator (op),
                              src->pixman_image, mask, dst->pixman_image,
                              extents->x + src_x, extents->y + src_y,
                              0, 0,
                              extents->x - dst_x, extents->y - dst_y,
                              extents->width, extents->height);

    pixman_image_unref (mask);

    return  CAIRO_STATUS_SUCCESS;
}

static void
set_point (pixman_point_fixed_t *p, cairo_point_t *c)
{
    p->x = _cairo_fixed_to_16_16 (c->x);
    p->y = _cairo_fixed_to_16_16 (c->y);
}

void
_pixman_image_add_tristrip (pixman_image_t *image,
			    int dst_x, int dst_y,
			    cairo_tristrip_t *strip)
{
    pixman_triangle_t tri;
    pixman_point_fixed_t *p[3] = {&tri.p1, &tri.p2, &tri.p3 };
    int n;

    set_point (p[0], &strip->points[0]);
    set_point (p[1], &strip->points[1]);
    set_point (p[2], &strip->points[2]);
    pixman_add_triangles (image, -dst_x, -dst_y, 1, &tri);
    for (n = 3; n < strip->num_points; n++) {
	set_point (p[n%3], &strip->points[n]);
	pixman_add_triangles (image, -dst_x, -dst_y, 1, &tri);
    }
}

static cairo_int_status_t
composite_tristrip (void			*_dst,
		    cairo_operator_t	op,
		    cairo_surface_t	*abstract_src,
		    int			src_x,
		    int			src_y,
		    int			dst_x,
		    int			dst_y,
		    const cairo_rectangle_int_t *extents,
		    cairo_antialias_t	antialias,
		    cairo_tristrip_t	*strip)
{
    cairo_image_surface_t *dst = (cairo_image_surface_t *) _dst;
    cairo_image_source_t *src = (cairo_image_source_t *) abstract_src;
    pixman_image_t *mask;
    pixman_format_code_t format;

    if (strip->num_points < 3)
	return CAIRO_STATUS_SUCCESS;

    format = antialias == CAIRO_ANTIALIAS_NONE ? PIXMAN_a1 : PIXMAN_a8;
    if (dst->pixman_format == format &&
	(abstract_src == NULL ||
	 (op == CAIRO_OPERATOR_ADD && src->is_opaque_solid)))
    {
	_pixman_image_add_tristrip (dst->pixman_image, dst_x, dst_y, strip);
	return CAIRO_STATUS_SUCCESS;
    }

    mask = pixman_image_create_bits (format,
				     extents->width, extents->height,
				     NULL, 0);
    if (unlikely (mask == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    _pixman_image_add_tristrip (mask, extents->x, extents->y, strip);
    pixman_image_composite32 (_pixman_operator (op),
                              src->pixman_image, mask, dst->pixman_image,
                              extents->x + src_x, extents->y + src_y,
                              0, 0,
                              extents->x - dst_x, extents->y - dst_y,
                              extents->width, extents->height);

    pixman_image_unref (mask);

    return  CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
check_composite_glyphs (const cairo_composite_rectangles_t *extents,
			cairo_scaled_font_t *scaled_font,
			cairo_glyph_t *glyphs,
			int *num_glyphs)
{
    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
composite_one_glyph (void				*_dst,
		     cairo_operator_t			 op,
		     cairo_surface_t			*_src,
		     int				 src_x,
		     int				 src_y,
		     int				 dst_x,
		     int				 dst_y,
		     cairo_composite_glyphs_info_t	 *info)
{
    cairo_image_surface_t *glyph_surface;
    cairo_scaled_glyph_t *scaled_glyph;
    cairo_status_t status;
    int x, y;

    status = _cairo_scaled_glyph_lookup (info->font,
					 info->glyphs[0].index,
					 CAIRO_SCALED_GLYPH_INFO_SURFACE,
					 &scaled_glyph);

    if (unlikely (status))
	return status;

    glyph_surface = scaled_glyph->surface;
    if (glyph_surface->width == 0 || glyph_surface->height == 0)
	return CAIRO_INT_STATUS_NOTHING_TO_DO;

    /* round glyph locations to the nearest pixel */
    /* XXX: FRAGILE: We're ignoring device_transform scaling here. A bug? */
    x = _cairo_lround (info->glyphs[0].x -
		       glyph_surface->base.device_transform.x0);
    y = _cairo_lround (info->glyphs[0].y -
		       glyph_surface->base.device_transform.y0);

    pixman_image_composite32 (_pixman_operator (op),
			      ((cairo_image_source_t *)_src)->pixman_image,
			      glyph_surface->pixman_image,
			      to_pixman_image (_dst),
			      x + src_x,  y + src_y,
			      0, 0,
			      x - dst_x, y - dst_y,
			      glyph_surface->width,
			      glyph_surface->height);

    return CAIRO_INT_STATUS_SUCCESS;
}

static cairo_int_status_t
composite_glyphs_via_mask (void				*_dst,
			   cairo_operator_t		 op,
			   cairo_surface_t		*_src,
			   int				 src_x,
			   int				 src_y,
			   int				 dst_x,
			   int				 dst_y,
			   cairo_composite_glyphs_info_t *info)
{
    cairo_scaled_glyph_t *glyph_cache[64];
    cairo_bool_t component_alpha = FALSE;
    uint8_t buf[2048];
    pixman_image_t *mask;
    cairo_status_t status;
    int i;

    /* XXX convert the glyphs to common formats a8/a8r8g8b8 to hit
     * optimised paths through pixman. Should we increase the bit
     * depth of the target surface, we should reconsider the appropriate
     * mask formats.
     */
    i = (info->extents.width + 3) & ~3;
    if (i * info->extents.height > (int) sizeof (buf)) {
	mask = pixman_image_create_bits (PIXMAN_a8,
					info->extents.width,
					info->extents.height,
					NULL, 0);
    } else {
	memset (buf, 0, i * info->extents.height);
	mask = pixman_image_create_bits (PIXMAN_a8,
					info->extents.width,
					info->extents.height,
					(uint32_t *)buf, i);
    }
    if (unlikely (mask == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    memset (glyph_cache, 0, sizeof (glyph_cache));
    status = CAIRO_STATUS_SUCCESS;

    for (i = 0; i < info->num_glyphs; i++) {
	cairo_image_surface_t *glyph_surface;
	cairo_scaled_glyph_t *scaled_glyph;
	unsigned long glyph_index = info->glyphs[i].index;
	int cache_index = glyph_index % ARRAY_LENGTH (glyph_cache);
	int x, y;

	scaled_glyph = glyph_cache[cache_index];
	if (scaled_glyph == NULL ||
	    _cairo_scaled_glyph_index (scaled_glyph) != glyph_index)
	{
	    status = _cairo_scaled_glyph_lookup (info->font, glyph_index,
						 CAIRO_SCALED_GLYPH_INFO_SURFACE,
						 &scaled_glyph);

	    if (unlikely (status)) {
		pixman_image_unref (mask);
		return status;
	    }

	    glyph_cache[cache_index] = scaled_glyph;
	}

	glyph_surface = scaled_glyph->surface;
	if (glyph_surface->width && glyph_surface->height) {
	    if (glyph_surface->base.content & CAIRO_CONTENT_COLOR &&
		! component_alpha) {
		pixman_image_t *ca_mask;

		ca_mask = pixman_image_create_bits (PIXMAN_a8r8g8b8,
						    info->extents.width,
						    info->extents.height,
						    NULL, 0);
		if (unlikely (ca_mask == NULL)) {
		    pixman_image_unref (mask);
		    return _cairo_error (CAIRO_STATUS_NO_MEMORY);
		}

		pixman_image_composite32 (PIXMAN_OP_SRC,
					  mask, 0, ca_mask,
					  0, 0,
					  0, 0,
					  0, 0,
					  info->extents.width,
					  info->extents.height);
		pixman_image_unref (mask);
		mask = ca_mask;
		component_alpha = TRUE;
	    }

	    /* round glyph locations to the nearest pixel */
	    /* XXX: FRAGILE: We're ignoring device_transform scaling here. A bug? */
	    x = _cairo_lround (info->glyphs[i].x -
			       glyph_surface->base.device_transform.x0);
	    y = _cairo_lround (info->glyphs[i].y -
			       glyph_surface->base.device_transform.y0);

	    pixman_image_composite32 (PIXMAN_OP_ADD,
				      glyph_surface->pixman_image, NULL, mask,
                                      0, 0,
				      0, 0,
                                      x - info->extents.x, y - info->extents.y,
				      glyph_surface->width,
				      glyph_surface->height);
	}
    }

    if (component_alpha)
	pixman_image_set_component_alpha (mask, TRUE);

    pixman_image_composite32 (_pixman_operator (op),
			      ((cairo_image_source_t *)_src)->pixman_image,
			      mask,
			      to_pixman_image (_dst),
			      info->extents.x + src_x, info->extents.y + src_y,
			      0, 0,
			      info->extents.x - dst_x, info->extents.y - dst_y,
			      info->extents.width, info->extents.height);
    pixman_image_unref (mask);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
composite_glyphs (void				*_dst,
		  cairo_operator_t		 op,
		  cairo_surface_t		*_src,
		  int				 src_x,
		  int				 src_y,
		  int				 dst_x,
		  int				 dst_y,
		  cairo_composite_glyphs_info_t *info)
{
    cairo_scaled_glyph_t *glyph_cache[64];
    pixman_image_t *dst, *src;
    cairo_status_t status;
    int i;

    if (info->num_glyphs == 1)
	return composite_one_glyph(_dst, op, _src, src_x, src_y, dst_x, dst_y, info);

    if (info->use_mask)
	return composite_glyphs_via_mask(_dst, op, _src, src_x, src_y, dst_x, dst_y, info);

    op = _pixman_operator (op);
    dst = to_pixman_image (_dst);
    src = ((cairo_image_source_t *)_src)->pixman_image;

    memset (glyph_cache, 0, sizeof (glyph_cache));
    status = CAIRO_STATUS_SUCCESS;

    for (i = 0; i < info->num_glyphs; i++) {
	int x, y;
	cairo_image_surface_t *glyph_surface;
	cairo_scaled_glyph_t *scaled_glyph;
	unsigned long glyph_index = info->glyphs[i].index;
	int cache_index = glyph_index % ARRAY_LENGTH (glyph_cache);

	scaled_glyph = glyph_cache[cache_index];
	if (scaled_glyph == NULL ||
	    _cairo_scaled_glyph_index (scaled_glyph) != glyph_index)
	{
	    status = _cairo_scaled_glyph_lookup (info->font, glyph_index,
						 CAIRO_SCALED_GLYPH_INFO_SURFACE,
						 &scaled_glyph);

	    if (unlikely (status))
		break;

	    glyph_cache[cache_index] = scaled_glyph;
	}

	glyph_surface = scaled_glyph->surface;
	if (glyph_surface->width && glyph_surface->height) {
	    /* round glyph locations to the nearest pixel */
	    /* XXX: FRAGILE: We're ignoring device_transform scaling here. A bug? */
	    x = _cairo_lround (info->glyphs[i].x -
			       glyph_surface->base.device_transform.x0);
	    y = _cairo_lround (info->glyphs[i].y -
			       glyph_surface->base.device_transform.y0);

	    pixman_image_composite32 (op, src, glyph_surface->pixman_image, dst,
                                      x + src_x,  y + src_y,
                                      0, 0,
                                      x - dst_x, y - dst_y,
				      glyph_surface->width,
				      glyph_surface->height);
	}
    }

    return status;
}

const cairo_compositor_t *
_cairo_image_traps_compositor_get (void)
{
    static cairo_traps_compositor_t compositor;

    if (compositor.base.delegate == NULL) {
	_cairo_traps_compositor_init (&compositor,
				      &__cairo_no_compositor);
	compositor.acquire = acquire;
	compositor.release = release;
	compositor.set_clip_region = set_clip_region;
	compositor.pattern_to_surface = _cairo_image_source_create_for_pattern;
	compositor.draw_image_boxes = draw_image_boxes;
	//compositor.copy_boxes = copy_boxes;
	compositor.fill_boxes = fill_boxes;
	//compositor.check_composite = check_composite;
	compositor.composite = composite;
	compositor.lerp = lerp;
	//compositor.check_composite_boxes = check_composite_boxes;
	compositor.composite_boxes = composite_boxes;
	//compositor.check_composite_traps = check_composite_traps;
	compositor.composite_traps = composite_traps;
	//compositor.check_composite_tristrip = check_composite_traps;
	compositor.composite_tristrip = composite_tristrip;
	compositor.check_composite_glyphs = check_composite_glyphs;
	compositor.composite_glyphs = composite_glyphs;
    }

    return &compositor.base;
}

const cairo_compositor_t *
_cairo_image_mask_compositor_get (void)
{
    static cairo_mask_compositor_t compositor;

    if (compositor.base.delegate == NULL) {
	_cairo_mask_compositor_init (&compositor,
				     _cairo_image_traps_compositor_get ());
	compositor.acquire = acquire;
	compositor.release = release;
	compositor.set_clip_region = set_clip_region;
	compositor.pattern_to_surface = _cairo_image_source_create_for_pattern;
	compositor.draw_image_boxes = draw_image_boxes;
	compositor.fill_rectangles = fill_rectangles;
	compositor.fill_boxes = fill_boxes;
	//compositor.check_composite = check_composite;
	compositor.composite = composite;
	//compositor.lerp = lerp;
	//compositor.check_composite_boxes = check_composite_boxes;
	compositor.composite_boxes = composite_boxes;
	compositor.check_composite_glyphs = check_composite_glyphs;
	compositor.composite_glyphs = composite_glyphs;
    }

    return &compositor.base;
}

#if PIXMAN_HAS_COMPOSITOR
typedef struct _cairo_image_span_renderer {
    cairo_span_renderer_t base;

    pixman_image_compositor_t *compositor;
    pixman_image_t *src, *mask;
    float opacity;
    cairo_rectangle_int_t extents;
} cairo_image_span_renderer_t;
COMPILE_TIME_ASSERT (sizeof (cairo_image_span_renderer_t) <= sizeof (cairo_abstract_span_renderer_t));

static cairo_status_t
_cairo_image_bounded_opaque_spans (void *abstract_renderer,
				   int y, int height,
				   const cairo_half_open_span_t *spans,
				   unsigned num_spans)
{
    cairo_image_span_renderer_t *r = abstract_renderer;

    if (num_spans == 0)
	return CAIRO_STATUS_SUCCESS;

    do {
	if (spans[0].coverage)
	    pixman_image_compositor_blt (r->compositor,
					 spans[0].x, y,
					 spans[1].x - spans[0].x, height,
					 spans[0].coverage);
	spans++;
    } while (--num_spans > 1);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_image_bounded_spans (void *abstract_renderer,
			    int y, int height,
			    const cairo_half_open_span_t *spans,
			    unsigned num_spans)
{
    cairo_image_span_renderer_t *r = abstract_renderer;

    if (num_spans == 0)
	return CAIRO_STATUS_SUCCESS;

    do {
	if (spans[0].coverage) {
	    pixman_image_compositor_blt (r->compositor,
					 spans[0].x, y,
					 spans[1].x - spans[0].x, height,
					 r->opacity * spans[0].coverage);
	}
	spans++;
    } while (--num_spans > 1);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_image_unbounded_spans (void *abstract_renderer,
			      int y, int height,
			      const cairo_half_open_span_t *spans,
			      unsigned num_spans)
{
    cairo_image_span_renderer_t *r = abstract_renderer;

    assert (y + height <= r->extents.height);
    if (y > r->extents.y) {
	pixman_image_compositor_blt (r->compositor,
				     r->extents.x, r->extents.y,
				     r->extents.width, y - r->extents.y,
				     0);
    }

    if (num_spans == 0) {
	pixman_image_compositor_blt (r->compositor,
				     r->extents.x, y,
				     r->extents.width,  height,
				     0);
    } else {
	if (spans[0].x != r->extents.x) {
	    pixman_image_compositor_blt (r->compositor,
					 r->extents.x, y,
					 spans[0].x - r->extents.x,
					 height,
					 0);
	}

	do {
	    assert (spans[0].x < r->extents.x + r->extents.width);
	    pixman_image_compositor_blt (r->compositor,
					 spans[0].x, y,
					 spans[1].x - spans[0].x, height,
					 r->opacity * spans[0].coverage);
	    spans++;
	} while (--num_spans > 1);

	if (spans[0].x != r->extents.x + r->extents.width) {
	    assert (spans[0].x < r->extents.x + r->extents.width);
	    pixman_image_compositor_blt (r->compositor,
					 spans[0].x,     y,
					 r->extents.x + r->extents.width - spans[0].x, height,
					 0);
	}
    }

    r->extents.y = y + height;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_image_clipped_spans (void *abstract_renderer,
			    int y, int height,
			    const cairo_half_open_span_t *spans,
			    unsigned num_spans)
{
    cairo_image_span_renderer_t *r = abstract_renderer;

    assert (num_spans);

    do {
	if (! spans[0].inverse)
	    pixman_image_compositor_blt (r->compositor,
					 spans[0].x, y,
					 spans[1].x - spans[0].x, height,
					 r->opacity * spans[0].coverage);
	spans++;
    } while (--num_spans > 1);

    r->extents.y = y + height;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_image_finish_unbounded_spans (void *abstract_renderer)
{
    cairo_image_span_renderer_t *r = abstract_renderer;

    if (r->extents.y < r->extents.height) {
	pixman_image_compositor_blt (r->compositor,
				     r->extents.x, r->extents.y,
				     r->extents.width,
				     r->extents.height - r->extents.y,
				     0);
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
span_renderer_init (cairo_abstract_span_renderer_t	*_r,
		    const cairo_composite_rectangles_t *composite,
		    cairo_bool_t			 needs_clip)
{
    cairo_image_span_renderer_t *r = (cairo_image_span_renderer_t *)_r;
    cairo_image_surface_t *dst = (cairo_image_surface_t *)composite->surface;
    const cairo_pattern_t *source = &composite->source_pattern.base;
    cairo_operator_t op = composite->op;
    int src_x, src_y;
    int mask_x, mask_y;

    if (op == CAIRO_OPERATOR_CLEAR) {
	op = PIXMAN_OP_LERP_CLEAR;
    } else if (dst->base.is_clear &&
	       (op == CAIRO_OPERATOR_SOURCE ||
		op == CAIRO_OPERATOR_OVER ||
		op == CAIRO_OPERATOR_ADD)) {
	op = PIXMAN_OP_SRC;
    } else if (op == CAIRO_OPERATOR_SOURCE) {
	op = PIXMAN_OP_LERP_SRC;
    } else {
	op = _pixman_operator (op);
    }

    r->compositor = NULL;
    r->mask = NULL;
    r->src = _pixman_image_for_pattern (dst, source, FALSE,
					&composite->unbounded,
					&composite->source_sample_area,
					&src_x, &src_y);
    if (unlikely (r->src == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    r->opacity = 1.0;
    if (composite->mask_pattern.base.type == CAIRO_PATTERN_TYPE_SOLID) {
	r->opacity = composite->mask_pattern.solid.color.alpha;
    } else {
	r->mask = _pixman_image_for_pattern (dst,
					     &composite->mask_pattern.base,
					     TRUE,
					     &composite->unbounded,
					     &composite->mask_sample_area,
					     &mask_x, &mask_y);
	if (unlikely (r->mask == NULL))
	    return _cairo_error (CAIRO_STATUS_NO_MEMORY);

	/* XXX Component-alpha? */
	if ((dst->base.content & CAIRO_CONTENT_COLOR) == 0 &&
	    _cairo_pattern_is_opaque (source, &composite->source_sample_area))
	{
	    pixman_image_unref (r->src);
	    r->src = r->mask;
	    src_x = mask_x;
	    src_y = mask_y;
	    r->mask = NULL;
	}
    }

    if (composite->is_bounded) {
	if (r->opacity == 1.)
	    r->base.render_rows = _cairo_image_bounded_opaque_spans;
	else
	    r->base.render_rows = _cairo_image_bounded_spans;
	r->base.finish = NULL;
    } else {
	if (needs_clip)
	    r->base.render_rows = _cairo_image_clipped_spans;
	else
	    r->base.render_rows = _cairo_image_unbounded_spans;
        r->base.finish = _cairo_image_finish_unbounded_spans;
	r->extents = composite->unbounded;
	r->extents.height += r->extents.y;
    }

    r->compositor =
	pixman_image_create_compositor (op, r->src, r->mask, dst->pixman_image,
					composite->unbounded.x + src_x,
					composite->unbounded.y + src_y,
					composite->unbounded.x + mask_x,
					composite->unbounded.y + mask_y,
					composite->unbounded.x,
					composite->unbounded.y,
					composite->unbounded.width,
					composite->unbounded.height);
    if (unlikely (r->compositor == NULL))
	return CAIRO_INT_STATUS_NOTHING_TO_DO;

    return CAIRO_STATUS_SUCCESS;
}

static void
span_renderer_fini (cairo_abstract_span_renderer_t *_r,
		    cairo_int_status_t status)
{
    cairo_image_span_renderer_t *r = (cairo_image_span_renderer_t *) _r;

    if (status == CAIRO_INT_STATUS_SUCCESS && r->base.finish)
	r->base.finish (r);

    if (r->compositor)
	pixman_image_compositor_destroy (r->compositor);

    if (r->src)
	pixman_image_unref (r->src);
    if (r->mask)
	pixman_image_unref (r->mask);
}
#else
typedef struct _cairo_image_span_renderer {
    cairo_span_renderer_t base;

    cairo_rectangle_int_t extents;

    float opacity;
    int stride;
    uint8_t *data;

    const cairo_composite_rectangles_t *composite;
    pixman_image_t *src, *mask;
    int src_x, src_y;

    uint8_t op;

    uint8_t buf[sizeof(cairo_abstract_span_renderer_t)-128];
} cairo_image_span_renderer_t;
COMPILE_TIME_ASSERT (sizeof (cairo_image_span_renderer_t) <= sizeof (cairo_abstract_span_renderer_t));

static cairo_status_t
_cairo_image_spans (void *abstract_renderer,
		    int y, int height,
		    const cairo_half_open_span_t *spans,
		    unsigned num_spans)
{
    cairo_image_span_renderer_t *r = abstract_renderer;
    uint8_t *mask, *row;
    int len;

    if (num_spans == 0)
	return CAIRO_STATUS_SUCCESS;

    mask = r->data + (y - r->extents.y) * r->stride;
    mask += spans[0].x - r->extents.x;
    row = mask;

    do {
	len = spans[1].x - spans[0].x;
	if (spans[0].coverage) {
	    *row++ = r->opacity * spans[0].coverage;
	    if (--len)
		memset (row, row[-1], len);
	}
	row += len;
	spans++;
    } while (--num_spans > 1);

    len = row - mask;
    row = mask;
    while (--height) {
	mask += r->stride;
	memcpy (mask, row, len);
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_image_spans_and_zero (void *abstract_renderer,
			     int y, int height,
			     const cairo_half_open_span_t *spans,
			     unsigned num_spans)
{
    cairo_image_span_renderer_t *r = abstract_renderer;
    uint8_t *mask;
    int len;

    mask = r->data;
    if (y > r->extents.y) {
	len = (y - r->extents.y) * r->stride;
	memset (mask, 0, len);
	mask += len;
    }

    r->extents.y = y + height;
    r->data = mask + height * r->stride;
    if (num_spans == 0) {
	memset (mask, 0, height * r->stride);
    } else {
	uint8_t *row = mask;

	if (spans[0].x != r->extents.x) {
	    len = spans[0].x - r->extents.x;
	    memset (row, 0, len);
	    row += len;
	}

	do {
	    len = spans[1].x - spans[0].x;
	    *row++ = r->opacity * spans[0].coverage;
	    if (len > 1) {
		memset (row, row[-1], --len);
		row += len;
	    }
	    spans++;
	} while (--num_spans > 1);

	if (spans[0].x != r->extents.x + r->extents.width) {
	    len = r->extents.x + r->extents.width - spans[0].x;
	    memset (row, 0, len);
	}

	row = mask;
	while (--height) {
	    mask += r->stride;
	    memcpy (mask, row, r->extents.width);
	}
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_image_finish_spans_and_zero (void *abstract_renderer)
{
    cairo_image_span_renderer_t *r = abstract_renderer;

    if (r->extents.y < r->extents.height)
	memset (r->data, 0, (r->extents.height - r->extents.y) * r->stride);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
span_renderer_init (cairo_abstract_span_renderer_t	*_r,
		    const cairo_composite_rectangles_t *composite,
		    cairo_bool_t			 needs_clip)
{
    cairo_image_span_renderer_t *r = (cairo_image_span_renderer_t *)_r;
    cairo_image_surface_t *dst = (cairo_image_surface_t *)composite->surface;
    const cairo_pattern_t *source = &composite->source_pattern.base;
    cairo_operator_t op = composite->op;

    r->composite = composite;
    r->mask = NULL;
    r->src = NULL;

    if (needs_clip)
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (op == CAIRO_OPERATOR_CLEAR) {
#if PIXMAN_HAS_OP_LERP
	op = PIXMAN_OP_LERP_CLEAR;
#else
	source = &_cairo_pattern_white.base;
	op = PIXMAN_OP_OUT_REVERSE;
#endif
    } else if (dst->base.is_clear &&
	       (op == CAIRO_OPERATOR_SOURCE ||
		op == CAIRO_OPERATOR_OVER ||
		op == CAIRO_OPERATOR_ADD)) {
	op = PIXMAN_OP_SRC;
    } else if (op == CAIRO_OPERATOR_SOURCE) {
#if PIXMAN_HAS_OP_LERP
	op = PIXMAN_OP_LERP_SRC;
#else
	return CAIRO_INT_STATUS_UNSUPPORTED;
#endif
    } else {
	op = _pixman_operator (op);
    }
    r->op = op;

    r->src = _pixman_image_for_pattern (dst, source, FALSE,
					&composite->unbounded,
					&composite->source_sample_area,
					&r->src_x, &r->src_y);
    if (unlikely (r->src == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    r->opacity = 1.0;
    if (composite->mask_pattern.base.type == CAIRO_PATTERN_TYPE_SOLID) {
	r->opacity = composite->mask_pattern.solid.color.alpha;
    } else {
	pixman_image_t *mask;
	int mask_x, mask_y;

	mask = _pixman_image_for_pattern (dst,
					  &composite->mask_pattern.base,
					  TRUE,
					  &composite->unbounded,
					  &composite->mask_sample_area,
					  &mask_x, &mask_y);
	if (unlikely (mask == NULL))
	    return _cairo_error (CAIRO_STATUS_NO_MEMORY);

	/* XXX Component-alpha? */
	if ((dst->base.content & CAIRO_CONTENT_COLOR) == 0 &&
	    _cairo_pattern_is_opaque (source, &composite->source_sample_area))
	{
	    pixman_image_unref (r->src);
	    r->src = mask;
	    r->src_x = mask_x;
	    r->src_y = mask_y;
	    mask = NULL;
	}

	if (mask) {
	    pixman_image_unref (mask);
	    return CAIRO_INT_STATUS_UNSUPPORTED;
	}
    }

    r->extents = composite->unbounded;
    r->stride = (r->extents.width + 3) & ~3;
    if (r->extents.height * r->stride > (int)sizeof (r->buf)) {
	r->mask = pixman_image_create_bits (PIXMAN_a8,
					    r->extents.width,
					    r->extents.height,
					    NULL, 0);

	r->base.render_rows = _cairo_image_spans;
	r->base.finish = NULL;
    } else {
	r->mask = pixman_image_create_bits (PIXMAN_a8,
					    r->extents.width,
					    r->extents.height,
					    (uint32_t *)r->buf, r->stride);

	r->base.render_rows = _cairo_image_spans_and_zero;
	r->base.finish = _cairo_image_finish_spans_and_zero;
    }
    if (unlikely (r->mask == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    r->data = (uint8_t *) pixman_image_get_data (r->mask);
    r->stride = pixman_image_get_stride (r->mask);

    r->extents.height += r->extents.y;
    return CAIRO_STATUS_SUCCESS;
}

static void
span_renderer_fini (cairo_abstract_span_renderer_t *_r,
		    cairo_int_status_t status)
{
    cairo_image_span_renderer_t *r = (cairo_image_span_renderer_t *) _r;

    if (likely (status == CAIRO_INT_STATUS_SUCCESS)) {
	const cairo_composite_rectangles_t *composite = r->composite;

	if (r->base.finish)
	    r->base.finish (r);

	pixman_image_composite32 (r->op, r->src, r->mask,
				  to_pixman_image (composite->surface),
				  composite->unbounded.x + r->src_x,
				  composite->unbounded.y + r->src_y,
				  0, 0,
				  composite->unbounded.x,
				  composite->unbounded.y,
				  composite->unbounded.width,
				  composite->unbounded.height);
    }

    if (r->src)
	pixman_image_unref (r->src);
    if (r->mask)
	pixman_image_unref (r->mask);
}
#endif

const cairo_compositor_t *
_cairo_image_spans_compositor_get (void)
{
    static cairo_spans_compositor_t compositor;

    if (compositor.base.delegate == NULL) {
	_cairo_spans_compositor_init (&compositor,
				      _cairo_image_traps_compositor_get());

	compositor.flags = 0;
#if PIXMAN_HAS_OP_LERP
	compositor.flags |= CAIRO_SPANS_COMPOSITOR_HAS_LERP;
#endif

	//compositor.acquire = acquire;
	//compositor.release = release;
	compositor.fill_boxes = fill_boxes;
	compositor.pattern_to_surface = _cairo_image_source_create_for_pattern;
	//compositor.check_composite_boxes = check_composite_boxes;
	compositor.composite_boxes = composite_boxes;
	//compositor.check_span_renderer = check_span_renderer;
	compositor.renderer_init = span_renderer_init;
	compositor.renderer_fini = span_renderer_fini;
    }

    return &compositor.base;
}
