/* -*- Mode: c; tab-width: 8; c-basic-offset: 4; indent-tabs-mode: t; -*- */
/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2002 University of Southern California
 * Copyright © 2005 Red Hat, Inc.
 * Copyright © 2011 Intel Corporation
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
 *      Joonas Pihlaja <jpihlaja@cc.helsinki.fi>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 */

#include "cairoint.h"

#include "cairo-compositor-private.h"
#include "cairo-clip-private.h"
#include "cairo-image-surface-private.h"
#include "cairo-paginated-private.h"
#include "cairo-pattern-private.h"
#include "cairo-region-private.h"
#include "cairo-recording-surface-private.h"
#include "cairo-spans-compositor-private.h"
#include "cairo-surface-subsurface-private.h"
#include "cairo-surface-snapshot-private.h"
#include "cairo-surface-observer-private.h"

typedef struct {
    cairo_polygon_t	*polygon;
    cairo_fill_rule_t	 fill_rule;
    cairo_antialias_t	 antialias;
} composite_spans_info_t;

static cairo_int_status_t
composite_polygon (const cairo_spans_compositor_t	*compositor,
		   cairo_composite_rectangles_t		 *extents,
		   cairo_polygon_t			*polygon,
		   cairo_fill_rule_t			 fill_rule,
		   cairo_antialias_t			 antialias);

static cairo_int_status_t
composite_boxes (const cairo_spans_compositor_t *compositor,
		 cairo_composite_rectangles_t *extents,
		 cairo_boxes_t		*boxes);

static cairo_int_status_t
clip_and_composite_polygon (const cairo_spans_compositor_t	*compositor,
			    cairo_composite_rectangles_t	 *extents,
			    cairo_polygon_t			*polygon,
			    cairo_fill_rule_t			 fill_rule,
			    cairo_antialias_t			 antialias);

static cairo_int_status_t
get_clip_polygon (const cairo_clip_path_t *clip_path,
		  cairo_antialias_t antialias,
		  cairo_polygon_t *polygon,
		  cairo_fill_rule_t *fill_rule)
{
    cairo_int_status_t status;
    cairo_bool_t is_first_polygon = TRUE;

    while (clip_path) {
	if (clip_path->antialias == antialias) {
	    if (is_first_polygon) {
		status = _cairo_path_fixed_fill_to_polygon (&clip_path->path,
							    clip_path->tolerance,
							    polygon);
		*fill_rule = clip_path->fill_rule;
		polygon->limits = NULL;
		polygon->num_limits = 0;
		is_first_polygon = FALSE;
	    } else if (polygon->num_edges == 0) {
		/* Intersecting with empty polygon will generate empty polygon. */
		return CAIRO_INT_STATUS_NOTHING_TO_DO;
	    } else {
		cairo_polygon_t next;

		_cairo_polygon_init (&next, NULL, 0);
		status = _cairo_path_fixed_fill_to_polygon (&clip_path->path,
							    clip_path->tolerance,
							    &next);
		if (likely (status == CAIRO_INT_STATUS_SUCCESS))
		    status = _cairo_polygon_intersect (polygon, *fill_rule,
						       &next, clip_path->fill_rule);
		_cairo_polygon_fini (&next);
		*fill_rule = CAIRO_FILL_RULE_WINDING;
	    }

	    if (unlikely (status))
		return status;
	}

	clip_path = clip_path->prev;
    }

    if (polygon->num_edges == 0)
	return CAIRO_INT_STATUS_NOTHING_TO_DO;

    assert (is_first_polygon == FALSE);
    return CAIRO_INT_STATUS_SUCCESS;
}

static cairo_int_status_t
composite_clip_polygon (const cairo_spans_compositor_t *compositor,
			cairo_surface_t *surface,
			cairo_operator_t op,
			cairo_polygon_t *polygon,
			cairo_fill_rule_t fill_rule,
			cairo_antialias_t antialias)
{
    cairo_int_status_t status;
    cairo_composite_rectangles_t composite;

    status = _cairo_composite_rectangles_init_for_polygon (&composite, surface, op,
							   &_cairo_pattern_white.base,
							   polygon,
							   NULL);
    if (unlikely (status))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    status = composite_polygon (compositor, &composite,
				polygon, fill_rule, antialias);
    _cairo_composite_rectangles_fini (&composite);

    return status;
}

static cairo_surface_t *
get_clip_surface (const cairo_spans_compositor_t *compositor,
		  cairo_surface_t *dst,
		  const cairo_clip_t *clip,
		  const cairo_rectangle_int_t *extents)
{
    cairo_surface_t *surface;
    cairo_box_t box;
    cairo_polygon_t polygon0;
    cairo_polygon_t polygon1;
    cairo_fill_rule_t fill_rule0;
    cairo_fill_rule_t fill_rule1;
    cairo_antialias_t antialias;
    cairo_int_status_t status;

    assert (clip->path);

    surface = _cairo_surface_create_similar_solid (dst,
						   CAIRO_CONTENT_ALPHA,
						   extents->width,
						   extents->height,
						   CAIRO_COLOR_TRANSPARENT);

    _cairo_box_from_rectangle (&box, extents);
    _cairo_polygon_init (&polygon0, &box, 1);
    _cairo_polygon_init (&polygon1, &box, 1);

    if (! surface)
	return _cairo_surface_create_in_error (CAIRO_INT_STATUS_NO_MEMORY);

    status = _cairo_clip_get_polygon (clip, &polygon0, &fill_rule0, &antialias);

    if (status != CAIRO_INT_STATUS_UNSUPPORTED) {
	if (status == CAIRO_INT_STATUS_NOTHING_TO_DO)
	    goto cleanup_polygon;

	_cairo_polygon_translate (&polygon0, -extents->x, -extents->y);
	status = composite_clip_polygon (compositor, surface, CAIRO_OPERATOR_ADD,
					 &polygon0, fill_rule0, antialias);
	_cairo_polygon_fini (&polygon0);
	_cairo_polygon_fini (&polygon1);

	if (unlikely (status))
	    goto cleanup_polygon;

	return surface;
    }

    /* Here, we can assume that clip paths have heterogeneous antialias,
     * as this code is fallback for that kind of clip.
     * This code might not work if all clip paths have equal antialias.
     */
    status = get_clip_polygon (clip->path, CAIRO_ANTIALIAS_DEFAULT, &polygon0, &fill_rule0);

    if (unlikely (status))
	goto cleanup_polygon;

    status = get_clip_polygon (clip->path, CAIRO_ANTIALIAS_NONE, &polygon1, &fill_rule1);

    if (unlikely (status))
	goto cleanup_polygon;

    _cairo_polygon_translate (&polygon0, -extents->x, -extents->y);
    status = composite_clip_polygon (compositor, surface, CAIRO_OPERATOR_ADD,
				     &polygon0, fill_rule0, CAIRO_ANTIALIAS_DEFAULT);

    if (unlikely (status))
	goto cleanup_polygon;

    _cairo_polygon_translate (&polygon1, -extents->x, -extents->y);
    status = composite_clip_polygon (compositor, surface, CAIRO_OPERATOR_IN,
				     &polygon1, fill_rule1, CAIRO_ANTIALIAS_NONE);

    if (unlikely (status))
	goto cleanup_polygon;

    _cairo_polygon_fini (&polygon0);
    _cairo_polygon_fini (&polygon1);

    return surface;

cleanup_polygon:
    _cairo_polygon_fini (&polygon0);
    _cairo_polygon_fini (&polygon1);
    cairo_surface_destroy (surface);

    return _cairo_int_surface_create_in_error (status);
}

static cairo_int_status_t
fixup_unbounded_mask (const cairo_spans_compositor_t *compositor,
		      const cairo_composite_rectangles_t *extents,
		      cairo_boxes_t *boxes)
{
    cairo_composite_rectangles_t composite;
    cairo_surface_t *clip;
    cairo_int_status_t status;

    clip = get_clip_surface (compositor, extents->surface, extents->clip,
			     &extents->unbounded);
    if (unlikely (clip->status)) {
	if ((cairo_int_status_t)clip->status == CAIRO_INT_STATUS_NOTHING_TO_DO)
	    return CAIRO_STATUS_SUCCESS;

	return clip->status;
    }

    status = _cairo_composite_rectangles_init_for_boxes (&composite,
							 extents->surface,
							 CAIRO_OPERATOR_CLEAR,
							 &_cairo_pattern_clear.base,
							 boxes,
							 NULL);
    if (unlikely (status))
	goto cleanup_clip;

    _cairo_pattern_init_for_surface (&composite.mask_pattern.surface, clip);
    composite.mask_pattern.base.filter = CAIRO_FILTER_NEAREST;
    composite.mask_pattern.base.extend = CAIRO_EXTEND_NONE;

    status = composite_boxes (compositor, &composite, boxes);

    _cairo_pattern_fini (&composite.mask_pattern.base);
    _cairo_composite_rectangles_fini (&composite);

cleanup_clip:
    cairo_surface_destroy (clip);
    return status;
}

static cairo_int_status_t
fixup_unbounded_polygon (const cairo_spans_compositor_t *compositor,
			 const cairo_composite_rectangles_t *extents,
			 cairo_boxes_t *boxes)
{
    cairo_polygon_t polygon, intersect;
    cairo_composite_rectangles_t composite;
    cairo_fill_rule_t fill_rule;
    cairo_antialias_t antialias;
    cairo_int_status_t status;

    /* Can we treat the clip as a regular clear-polygon and use it to fill? */
    status = _cairo_clip_get_polygon (extents->clip, &polygon,
				      &fill_rule, &antialias);
    if (status == CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

    status= _cairo_polygon_init_boxes (&intersect, boxes);
    if (unlikely (status))
	goto cleanup_polygon;

    status = _cairo_polygon_intersect (&polygon, fill_rule,
				       &intersect, CAIRO_FILL_RULE_WINDING);
    _cairo_polygon_fini (&intersect);

    if (unlikely (status))
	goto cleanup_polygon;

    status = _cairo_composite_rectangles_init_for_polygon (&composite,
							   extents->surface,
							   CAIRO_OPERATOR_CLEAR,
							   &_cairo_pattern_clear.base,
							   &polygon,
							   NULL);
    if (unlikely (status))
	goto cleanup_polygon;

    status = composite_polygon (compositor, &composite,
				&polygon, fill_rule, antialias);

    _cairo_composite_rectangles_fini (&composite);
cleanup_polygon:
    _cairo_polygon_fini (&polygon);

    return status;
}

static cairo_int_status_t
fixup_unbounded_boxes (const cairo_spans_compositor_t *compositor,
		       const cairo_composite_rectangles_t *extents,
		       cairo_boxes_t *boxes)
{
    cairo_boxes_t tmp, clear;
    cairo_box_t box;
    cairo_int_status_t status;

    assert (boxes->is_pixel_aligned);

    if (extents->bounded.width  == extents->unbounded.width &&
	extents->bounded.height == extents->unbounded.height)
    {
	return CAIRO_STATUS_SUCCESS;
    }

    /* subtract the drawn boxes from the unbounded area */
    _cairo_boxes_init (&clear);

    box.p1.x = _cairo_fixed_from_int (extents->unbounded.x + extents->unbounded.width);
    box.p1.y = _cairo_fixed_from_int (extents->unbounded.y);
    box.p2.x = _cairo_fixed_from_int (extents->unbounded.x);
    box.p2.y = _cairo_fixed_from_int (extents->unbounded.y + extents->unbounded.height);

    if (boxes->num_boxes) {
	_cairo_boxes_init (&tmp);

	status = _cairo_boxes_add (&tmp, CAIRO_ANTIALIAS_DEFAULT, &box);
	assert (status == CAIRO_INT_STATUS_SUCCESS);

	tmp.chunks.next = &boxes->chunks;
	tmp.num_boxes += boxes->num_boxes;

	status = _cairo_bentley_ottmann_tessellate_boxes (&tmp,
							  CAIRO_FILL_RULE_WINDING,
							  &clear);
	tmp.chunks.next = NULL;
	if (unlikely (status))
	    goto error;
    } else {
	box.p1.x = _cairo_fixed_from_int (extents->unbounded.x);
	box.p2.x = _cairo_fixed_from_int (extents->unbounded.x + extents->unbounded.width);

	status = _cairo_boxes_add (&clear, CAIRO_ANTIALIAS_DEFAULT, &box);
	assert (status == CAIRO_INT_STATUS_SUCCESS);
    }

    /* Now intersect with the clip boxes */
    if (extents->clip->num_boxes) {
	_cairo_boxes_init_for_array (&tmp,
				     extents->clip->boxes,
				     extents->clip->num_boxes);
	status = _cairo_boxes_intersect (&clear, &tmp, &clear);
	if (unlikely (status))
	    goto error;
    }

    /* If we have a clip polygon, we need to intersect with that as well */
    if (extents->clip->path) {
	status = fixup_unbounded_polygon (compositor, extents, &clear);
	if (status == CAIRO_INT_STATUS_UNSUPPORTED)
	    status = fixup_unbounded_mask (compositor, extents, &clear);
    } else {
	status = compositor->fill_boxes (extents->surface,
					 CAIRO_OPERATOR_CLEAR,
					 CAIRO_COLOR_TRANSPARENT,
					 &clear);
    }

error:
    _cairo_boxes_fini (&clear);
    return status;
}

static cairo_surface_t *
unwrap_surface (const cairo_pattern_t *pattern)
{
    cairo_surface_t *surface;

    surface = ((const cairo_surface_pattern_t *) pattern)->surface;
    if (_cairo_surface_is_paginated (surface))
	surface = _cairo_paginated_surface_get_recording (surface);
    if (_cairo_surface_is_snapshot (surface))
	surface = _cairo_surface_snapshot_get_target (surface);
    if (_cairo_surface_is_observer (surface))
	surface = _cairo_surface_observer_get_target (surface);
    return surface;
}

static cairo_bool_t
is_recording_pattern (const cairo_pattern_t *pattern)
{
    cairo_surface_t *surface;

    if (pattern->type != CAIRO_PATTERN_TYPE_SURFACE)
	return FALSE;

    surface = ((const cairo_surface_pattern_t *) pattern)->surface;
    return _cairo_surface_is_recording (surface);
}

static cairo_bool_t
recording_pattern_contains_sample (const cairo_pattern_t *pattern,
				   const cairo_rectangle_int_t *sample)
{
    cairo_recording_surface_t *surface;

    if (! is_recording_pattern (pattern))
	return FALSE;

    if (pattern->extend == CAIRO_EXTEND_NONE)
	return TRUE;

    surface = (cairo_recording_surface_t *) unwrap_surface (pattern);
    if (surface->unbounded)
	return TRUE;

    if (sample->x >= surface->extents.x &&
	sample->y >= surface->extents.y &&
	sample->x + sample->width <= surface->extents.x + surface->extents.width &&
	sample->y + sample->height <= surface->extents.y + surface->extents.height)
    {
	return TRUE;
    }

    return FALSE;
}

static cairo_bool_t
op_reduces_to_source (const cairo_composite_rectangles_t *extents)
{
    if (extents->op == CAIRO_OPERATOR_SOURCE)
	return TRUE;

    if (extents->surface->is_clear)
	return extents->op == CAIRO_OPERATOR_OVER || extents->op == CAIRO_OPERATOR_ADD;

    return FALSE;
}

static cairo_int_status_t
composite_aligned_boxes (const cairo_spans_compositor_t		*compositor,
			 const cairo_composite_rectangles_t	*extents,
			 cairo_boxes_t				*boxes)
{
    cairo_surface_t *dst = extents->surface;
    cairo_operator_t op = extents->op;
    const cairo_pattern_t *source = &extents->source_pattern.base;
    cairo_int_status_t status;
    cairo_bool_t need_clip_mask = extents->clip->path != NULL;
    cairo_bool_t op_is_source;
    cairo_bool_t no_mask;
    cairo_bool_t inplace;

    if (need_clip_mask && ! extents->is_bounded)
	return CAIRO_INT_STATUS_UNSUPPORTED;

    op_is_source = op_reduces_to_source (extents);
    no_mask = extents->mask_pattern.base.type == CAIRO_PATTERN_TYPE_SOLID &&
	CAIRO_ALPHA_IS_OPAQUE (extents->mask_pattern.solid.color.alpha);
    inplace = ! need_clip_mask && op_is_source && no_mask;

    if (op == CAIRO_OPERATOR_SOURCE && (need_clip_mask || ! no_mask)) {
	/* SOURCE with a mask is actually a LERP in cairo semantics */
	if ((compositor->flags & CAIRO_SPANS_COMPOSITOR_HAS_LERP) == 0)
	    return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    /* Are we just copying a recording surface? */
    if (inplace &&
	recording_pattern_contains_sample (&extents->source_pattern.base,
					   &extents->source_sample_area))
    {
	cairo_clip_t *recording_clip;
	const cairo_pattern_t *source = &extents->source_pattern.base;

	/* XXX could also do tiling repeat modes... */

	/* first clear the area about to be overwritten */
	if (! dst->is_clear)
	    status = compositor->fill_boxes (dst,
					     CAIRO_OPERATOR_CLEAR,
					     CAIRO_COLOR_TRANSPARENT,
					     boxes);

	recording_clip = _cairo_clip_from_boxes (boxes);
	status = _cairo_recording_surface_replay_with_clip (unwrap_surface (source),
							    &source->matrix,
							    dst, recording_clip);
	_cairo_clip_destroy (recording_clip);

	return status;
    }

    if (! need_clip_mask && no_mask && source->type == CAIRO_PATTERN_TYPE_SOLID) {
	const cairo_color_t *color;

	color = &((cairo_solid_pattern_t *) source)->color;
	if (op_is_source)
	    op = CAIRO_OPERATOR_SOURCE;
	status = compositor->fill_boxes (dst, op, color, boxes);
#if 0
    } else if (inplace && source->type == CAIRO_PATTERN_TYPE_SURFACE) {
	status = upload_inplace (compositor, extents, boxes);
#endif
    } else {
	cairo_surface_t *src;
	cairo_surface_t *mask = NULL;
	int src_x, src_y;
	int mask_x = 0, mask_y = 0;

	/* All typical cases will have been resolved before now... */
	if (need_clip_mask) {
	    mask = get_clip_surface (compositor, dst, extents->clip,
				     &extents->bounded);
	    if (unlikely (mask->status)) {
		if ((cairo_int_status_t)mask->status == CAIRO_INT_STATUS_NOTHING_TO_DO)
		    return CAIRO_STATUS_SUCCESS;

		return mask->status;
	    }

	    mask_x = -extents->bounded.x;
	    mask_y = -extents->bounded.y;
	}

	/* XXX but this is still ugly */
	if (! no_mask) {
	    src = compositor->pattern_to_surface (dst,
						  &extents->mask_pattern.base,
						  TRUE,
						  &extents->bounded,
						  &extents->mask_sample_area,
						  &src_x, &src_y);
	    if (unlikely (src->status)) {
		cairo_surface_destroy (mask);
		return src->status;
	    }

	    if (mask != NULL) {
		status = compositor->composite_boxes (mask, CAIRO_OPERATOR_IN,
						      src, NULL,
						      src_x, src_y,
						      0, 0,
						      mask_x, mask_y,
						      boxes, &extents->bounded);

		cairo_surface_destroy (src);
	    } else {
		mask = src;
		mask_x = src_x;
		mask_y = src_y;
	    }
	}

	src = compositor->pattern_to_surface (dst, source, FALSE,
					      &extents->bounded,
					      &extents->source_sample_area,
					      &src_x, &src_y);
	if (likely (src->status == CAIRO_STATUS_SUCCESS)) {
	    status = compositor->composite_boxes (dst, op, src, mask,
						  src_x, src_y,
						  mask_x, mask_y,
						  0, 0,
						  boxes, &extents->bounded);
	    cairo_surface_destroy (src);
	} else
	    status = src->status;

	cairo_surface_destroy (mask);
    }

    if (status == CAIRO_INT_STATUS_SUCCESS && ! extents->is_bounded)
	status = fixup_unbounded_boxes (compositor, extents, boxes);

    return status;
}

static cairo_bool_t
composite_needs_clip (const cairo_composite_rectangles_t *composit,
		      const cairo_box_t *extents)
{
    cairo_bool_t needs_clip;

    if (composit->clip && composit->clip->path != NULL)
	return TRUE;

    needs_clip = ! composit->is_bounded;
    if (needs_clip)
	needs_clip = ! _cairo_clip_contains_box (composit->clip, extents);

    return needs_clip;
}

static cairo_int_status_t
composite_boxes (const cairo_spans_compositor_t *compositor,
		 cairo_composite_rectangles_t *extents,
		 cairo_boxes_t		*boxes)
{
    cairo_abstract_span_renderer_t renderer;
    cairo_rectangular_scan_converter_t converter;
    const struct _cairo_boxes_chunk *chunk;
    cairo_int_status_t status;
    cairo_box_t box;

    _cairo_box_from_rectangle (&box, &extents->bounded);
    if (composite_needs_clip (extents, &box))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    _cairo_rectangular_scan_converter_init (&converter, &extents->bounded);
    for (chunk = &boxes->chunks; chunk != NULL; chunk = chunk->next) {
	const cairo_box_t *box = chunk->base;
	int i;

	for (i = 0; i < chunk->count; i++) {
	    status = _cairo_rectangular_scan_converter_add_box (&converter, &box[i], 1);
	    if (unlikely (status))
		goto cleanup_converter;
	}
    }

    status = compositor->renderer_init (&renderer, extents, FALSE);
    if (likely (status == CAIRO_INT_STATUS_SUCCESS))
	status = converter.base.generate (&converter.base, &renderer.base);
    compositor->renderer_fini (&renderer, status);

cleanup_converter:
    converter.base.destroy (&converter.base);
    return status;
}

static cairo_int_status_t
composite_polygon (const cairo_spans_compositor_t	*compositor,
		   cairo_composite_rectangles_t		 *extents,
		   cairo_polygon_t			*polygon,
		   cairo_fill_rule_t			 fill_rule,
		   cairo_antialias_t			 antialias)
{
    cairo_abstract_span_renderer_t renderer;
    cairo_scan_converter_t *converter;
    cairo_bool_t needs_clip;
    cairo_int_status_t status;

    needs_clip = composite_needs_clip (extents, &polygon->extents);
    if (needs_clip) {
	return CAIRO_INT_STATUS_UNSUPPORTED;
	converter = _cairo_clip_tor_scan_converter_create (extents->clip,
							   polygon,
							   fill_rule, antialias);
    } else {
	const cairo_rectangle_int_t *r = &extents->bounded;

	if (antialias == CAIRO_ANTIALIAS_FAST) {
	    converter = _cairo_tor22_scan_converter_create (r->x, r->y,
							    r->x + r->width,
							    r->y + r->height,
							    fill_rule, antialias);
	    status = _cairo_tor22_scan_converter_add_polygon (converter, polygon);
	} else if (antialias == CAIRO_ANTIALIAS_NONE) {
	    converter = _cairo_mono_scan_converter_create (r->x, r->y,
							   r->x + r->width,
							   r->y + r->height,
							   fill_rule);
	    status = _cairo_mono_scan_converter_add_polygon (converter, polygon);
	} else {
	    converter = _cairo_tor_scan_converter_create (r->x, r->y,
							  r->x + r->width,
							  r->y + r->height,
							  fill_rule, antialias);
	    status = _cairo_tor_scan_converter_add_polygon (converter, polygon);
	}
    }
    if (unlikely (status))
	goto cleanup_converter;

    status = compositor->renderer_init (&renderer, extents, needs_clip);
    if (likely (status == CAIRO_INT_STATUS_SUCCESS))
	status = converter->generate (converter, &renderer.base);
    compositor->renderer_fini (&renderer, status);

cleanup_converter:
    converter->destroy (converter);
    return status;
}

static cairo_int_status_t
trim_extents_to_boxes (cairo_composite_rectangles_t *extents,
		       cairo_boxes_t *boxes)
{
    cairo_box_t box;

    _cairo_boxes_extents (boxes, &box);
    return _cairo_composite_rectangles_intersect_mask_extents (extents, &box);
}

static cairo_int_status_t
trim_extents_to_polygon (cairo_composite_rectangles_t *extents,
			 cairo_polygon_t *polygon)
{
    return _cairo_composite_rectangles_intersect_mask_extents (extents,
							       &polygon->extents);
}

static cairo_int_status_t
clip_and_composite_boxes (const cairo_spans_compositor_t	*compositor,
			  cairo_composite_rectangles_t		*extents,
			  cairo_boxes_t				*boxes)
{
    cairo_int_status_t status;
    cairo_polygon_t polygon;
    struct _cairo_boxes_chunk *chunk;
    int i;

    status = trim_extents_to_boxes (extents, boxes);
    if (unlikely (status))
	return status;

    if (boxes->num_boxes == 0) {
	if (extents->is_bounded)
	    return CAIRO_STATUS_SUCCESS;

	return fixup_unbounded_boxes (compositor, extents, boxes);
    }

    /* Can we reduce drawing through a clip-mask to simply drawing the clip? */
    if (extents->clip->path != NULL && extents->is_bounded) {
	cairo_polygon_t polygon;
	cairo_fill_rule_t fill_rule;
	cairo_antialias_t antialias;
	cairo_clip_t *clip;

	clip = _cairo_clip_copy (extents->clip);
	clip = _cairo_clip_intersect_boxes (clip, boxes);
	status = _cairo_clip_get_polygon (clip, &polygon,
					  &fill_rule, &antialias);
	_cairo_clip_path_destroy (clip->path);
	clip->path = NULL;
	if (likely (status == CAIRO_INT_STATUS_SUCCESS)) {
	    cairo_clip_t *saved_clip = extents->clip;
	    extents->clip = clip;

	    status = clip_and_composite_polygon (compositor, extents, &polygon,
						 fill_rule, antialias);

	    clip = extents->clip;
	    extents->clip = saved_clip;

	    _cairo_polygon_fini (&polygon);
	}
	_cairo_clip_destroy (clip);

	if (status != CAIRO_INT_STATUS_UNSUPPORTED)
	    return status;
    }

    if (boxes->is_pixel_aligned) {
	status = composite_aligned_boxes (compositor, extents, boxes);
	if (status != CAIRO_INT_STATUS_UNSUPPORTED)
	    return status;
    }

    status = composite_boxes (compositor, extents, boxes);
    if (status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

    status = _cairo_polygon_init_boxes (&polygon, boxes);
    if (unlikely (status))
	return status;

    status = composite_polygon (compositor, extents, &polygon,
				CAIRO_FILL_RULE_WINDING,
				CAIRO_ANTIALIAS_DEFAULT);
    _cairo_polygon_fini (&polygon);

    return status;
}

static cairo_int_status_t
clip_and_composite_polygon (const cairo_spans_compositor_t	*compositor,
			    cairo_composite_rectangles_t	 *extents,
			    cairo_polygon_t			*polygon,
			    cairo_fill_rule_t			 fill_rule,
			    cairo_antialias_t			 antialias)
{
    cairo_int_status_t status;

    /* XXX simply uses polygon limits.point extemities, tessellation? */
    status = trim_extents_to_polygon (extents, polygon);
    if (unlikely (status))
	return status;

    if (_cairo_polygon_is_empty (polygon)) {
	cairo_boxes_t boxes;

	if (extents->is_bounded)
	    return CAIRO_STATUS_SUCCESS;

	_cairo_boxes_init (&boxes);
	extents->bounded.width = extents->bounded.height = 0;
	return fixup_unbounded_boxes (compositor, extents, &boxes);
    }

    if (extents->is_bounded && extents->clip->path) {
	cairo_polygon_t clipper;
	cairo_antialias_t clip_antialias;
	cairo_fill_rule_t clip_fill_rule;

	status = _cairo_clip_get_polygon (extents->clip,
					  &clipper,
					  &clip_fill_rule,
					  &clip_antialias);
	if (likely (status == CAIRO_INT_STATUS_SUCCESS)) {
	    cairo_clip_t *old_clip;

	    if (clip_antialias == antialias) {
		/* refresh limits after trimming extents */
		_cairo_polygon_limit_to_clip(polygon, extents->clip);

		status = _cairo_polygon_intersect (polygon, fill_rule,
						   &clipper, clip_fill_rule);
		_cairo_polygon_fini (&clipper);
		if (unlikely (status))
		    return status;

		old_clip = extents->clip;
		extents->clip = _cairo_clip_copy_region (extents->clip);
		_cairo_clip_destroy (old_clip);
	    } else {
		_cairo_polygon_fini (&clipper);
	    }
	}
    }

    return composite_polygon (compositor, extents,
			      polygon, fill_rule, antialias);
}

/* high-level compositor interface */

static cairo_int_status_t
_cairo_spans_compositor_paint (const cairo_compositor_t		*_compositor,
			       cairo_composite_rectangles_t	*extents)
{
    const cairo_spans_compositor_t *compositor = (cairo_spans_compositor_t*)_compositor;
    cairo_boxes_t boxes;
    cairo_int_status_t status;

    _cairo_clip_steal_boxes (extents->clip, &boxes);
    status = clip_and_composite_boxes (compositor, extents, &boxes);
    _cairo_clip_unsteal_boxes (extents->clip, &boxes);

    return status;
}

static cairo_int_status_t
_cairo_spans_compositor_mask (const cairo_compositor_t		*_compositor,
			      cairo_composite_rectangles_t	*extents)
{
    const cairo_spans_compositor_t *compositor = (cairo_spans_compositor_t*)_compositor;
    cairo_int_status_t status;
    cairo_boxes_t boxes;

    _cairo_clip_steal_boxes (extents->clip, &boxes);
    status = clip_and_composite_boxes (compositor, extents, &boxes);
    _cairo_clip_unsteal_boxes (extents->clip, &boxes);

    return status;
}

static cairo_int_status_t
_cairo_spans_compositor_stroke (const cairo_compositor_t	*_compositor,
				cairo_composite_rectangles_t	 *extents,
				const cairo_path_fixed_t	*path,
				const cairo_stroke_style_t	*style,
				const cairo_matrix_t		*ctm,
				const cairo_matrix_t		*ctm_inverse,
				double				 tolerance,
				cairo_antialias_t		 antialias)
{
    const cairo_spans_compositor_t *compositor = (cairo_spans_compositor_t*)_compositor;
    cairo_int_status_t status;

    status = CAIRO_INT_STATUS_UNSUPPORTED;
    if (_cairo_path_fixed_stroke_is_rectilinear (path)) {
	cairo_boxes_t boxes;

	_cairo_boxes_init (&boxes);
	if (! _cairo_clip_contains_rectangle (extents->clip, &extents->mask))
	    _cairo_boxes_limit (&boxes,
				extents->clip->boxes,
				extents->clip->num_boxes);

	status = _cairo_path_fixed_stroke_rectilinear_to_boxes (path,
								style,
								ctm,
								antialias,
								&boxes);
	if (likely (status == CAIRO_INT_STATUS_SUCCESS))
	    status = clip_and_composite_boxes (compositor, extents, &boxes);
	_cairo_boxes_fini (&boxes);
    }

    if (status == CAIRO_INT_STATUS_UNSUPPORTED) {
	cairo_polygon_t polygon;

	if (extents->mask.width  > extents->unbounded.width ||
	    extents->mask.height > extents->unbounded.height)
	{
	    _cairo_polygon_init_with_clip (&polygon, extents->clip);
	}
	else
	{
	    _cairo_polygon_init_with_clip (&polygon, NULL);
	}
	status = _cairo_path_fixed_stroke_to_polygon (path,
						      style,
						      ctm, ctm_inverse,
						      tolerance,
						      &polygon);
	if (likely (status == CAIRO_INT_STATUS_SUCCESS)) {
	    status = clip_and_composite_polygon (compositor, extents, &polygon,
						 CAIRO_FILL_RULE_WINDING,
						 antialias);
	}
	_cairo_polygon_fini (&polygon);
    }

    return status;
}

static cairo_int_status_t
_cairo_spans_compositor_fill (const cairo_compositor_t		*_compositor,
			      cairo_composite_rectangles_t	 *extents,
			      const cairo_path_fixed_t		*path,
			      cairo_fill_rule_t			 fill_rule,
			      double				 tolerance,
			      cairo_antialias_t			 antialias)
{
    const cairo_spans_compositor_t *compositor = (cairo_spans_compositor_t*)_compositor;
    cairo_int_status_t status;

    status = CAIRO_INT_STATUS_UNSUPPORTED;
    if (_cairo_path_fixed_fill_is_rectilinear (path)) {
	cairo_boxes_t boxes;

	_cairo_boxes_init (&boxes);
	if (! _cairo_clip_contains_rectangle (extents->clip, &extents->mask))
	    _cairo_boxes_limit (&boxes,
				extents->clip->boxes,
				extents->clip->num_boxes);
	status = _cairo_path_fixed_fill_rectilinear_to_boxes (path,
							      fill_rule,
							      antialias,
							      &boxes);
	if (likely (status == CAIRO_INT_STATUS_SUCCESS))
	    status = clip_and_composite_boxes (compositor, extents, &boxes);
	_cairo_boxes_fini (&boxes);
    }
    if (status == CAIRO_INT_STATUS_UNSUPPORTED) {
	cairo_polygon_t polygon;

	if (extents->mask.width  > extents->unbounded.width ||
	    extents->mask.height > extents->unbounded.height)
	{
	    cairo_box_t limits;
	    _cairo_box_from_rectangle (&limits, &extents->unbounded);
	    _cairo_polygon_init (&polygon, &limits, 1);
	}
	else
	{
	    _cairo_polygon_init (&polygon, NULL, 0);
	}

	status = _cairo_path_fixed_fill_to_polygon (path, tolerance, &polygon);
	if (likely (status == CAIRO_INT_STATUS_SUCCESS)) {
	    status = _cairo_polygon_intersect_with_boxes (&polygon, &fill_rule,
							  extents->clip->boxes,
							  extents->clip->num_boxes);
	}
	if (likely (status == CAIRO_INT_STATUS_SUCCESS)) {
	    if (extents->is_bounded) {
		if (extents->clip->boxes != &extents->clip->embedded_box)
		    free (extents->clip->boxes);

		extents->clip->num_boxes = 1;
		extents->clip->boxes = &extents->clip->embedded_box;
		extents->clip->boxes[0] = polygon.extents;
	    }
	    status = clip_and_composite_polygon (compositor, extents, &polygon,
						 fill_rule, antialias);
	}
	_cairo_polygon_fini (&polygon);
    }

    return status;
}

void
_cairo_spans_compositor_init (cairo_spans_compositor_t *compositor,
			      const cairo_compositor_t  *delegate)
{
    compositor->base.delegate = delegate;

    compositor->base.paint  = _cairo_spans_compositor_paint;
    compositor->base.mask   = _cairo_spans_compositor_mask;
    compositor->base.fill   = _cairo_spans_compositor_fill;
    compositor->base.stroke = _cairo_spans_compositor_stroke;
    compositor->base.glyphs = NULL;
}
