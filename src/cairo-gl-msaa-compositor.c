/* -*- Mode: c; tab-width: 8; c-basic-offset: 4; indent-tabs-mode: t; -*- */
/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2002 University of Southern California
 * Copyright © 2005 Red Hat, Inc.
 * Copyright © 2011 Intel Corporation
 * Copyright © 2011 Samsung Electronics
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
 *	Henry Song <hsong@sisa.samsung.com>
 *	Martin Robinson <mrobinson@igalia.com>
 */

#include "cairoint.h"

#include "cairo-clip-inline.h"
#include "cairo-composite-rectangles-private.h"
#include "cairo-compositor-private.h"
#include "cairo-gl-private.h"
#include "cairo-traps-private.h"
#include "cairo-surface-subsurface-inline.h"

static cairo_bool_t
should_fall_back (cairo_gl_surface_t *surface,
		  cairo_antialias_t antialias);

struct _tristrip_composite_info {
    cairo_gl_composite_t	setup;
    cairo_gl_context_t		*ctx;
};

static cairo_int_status_t
_draw_trap (cairo_gl_context_t		*ctx,
	    cairo_gl_composite_t	*setup,
	    cairo_trapezoid_t		*trap)
{
    cairo_point_t quad[4];

    quad[0].x = _cairo_edge_compute_intersection_x_for_y (&trap->left.p1,
							  &trap->left.p2,
							  trap->top);
    quad[0].y = trap->top;

    quad[1].x = _cairo_edge_compute_intersection_x_for_y (&trap->left.p1,
						      &trap->left.p2,
						      trap->bottom);
    quad[1].y = trap->bottom;

    quad[2].x = _cairo_edge_compute_intersection_x_for_y (&trap->right.p1,
						      &trap->right.p2,
						      trap->bottom);
    quad[2].y = trap->bottom;

    quad[3].x = _cairo_edge_compute_intersection_x_for_y (&trap->right.p1,
						      &trap->right.p2,
						      trap->top);
    quad[3].y = trap->top;
    return _cairo_gl_composite_emit_quad_as_tristrip (ctx, setup, quad);
}

static cairo_int_status_t
_draw_traps (cairo_gl_context_t		*ctx,
	     cairo_gl_composite_t	*setup,
	     cairo_traps_t		*traps)
{
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;
    int i;

    for (i = 0; i < traps->num_traps; i++) {
	cairo_trapezoid_t *trap = traps->traps + i;
	if (unlikely ((status = _draw_trap (ctx, setup, trap))))
	    return status;
    }

   return status;
}

static cairo_int_status_t
_cairo_gl_msaa_compositor_draw_quad (cairo_gl_context_t 	*ctx,
				     cairo_gl_composite_t	*setup,
				     cairo_box_t		*box)
{
    cairo_point_t quad[4];

    quad[0].x = box->p1.x;
    quad[0].y = box->p1.y;

    quad[1].x = box->p1.x;
    quad[1].y = box->p2.y;

    quad[2].x = box->p2.x;
    quad[2].y = box->p2.y;

    quad[3].x = box->p2.x;
    quad[3].y = box->p1.y;

    return _cairo_gl_composite_emit_quad_as_tristrip (ctx, setup, quad);
}

static cairo_int_status_t
_draw_int_rect (cairo_gl_context_t	*ctx,
		cairo_gl_composite_t	*setup,
		cairo_rectangle_int_t	*rect)
{
    cairo_box_t box;

    _cairo_box_from_rectangle (&box, rect);

    return _cairo_gl_msaa_compositor_draw_quad (ctx, setup, &box);
}

static cairo_int_status_t
_draw_triangle_fan (cairo_gl_context_t		*ctx,
		    cairo_gl_composite_t	*setup,
		    const cairo_point_t		*midpt,
		    const cairo_point_t		*points,
		    int				 npoints)
{
    int i;

    /* Our strategy here is to not even try to build a triangle fan, but to
       draw each triangle as if it was an unconnected member of a triangle strip. */
    for (i = 1; i < npoints; i++) {
	cairo_int_status_t status;
	cairo_point_t triangle[3];

	triangle[0] = *midpt;
	triangle[1] = points[i - 1];
	triangle[2] = points[i];

	status = _cairo_gl_composite_emit_triangle_as_tristrip (ctx, setup, triangle);
	if (unlikely (status))
	    return status;
    }

    return CAIRO_STATUS_SUCCESS;
}

cairo_int_status_t
_cairo_gl_msaa_compositor_draw_clip (cairo_gl_context_t		*ctx,
				     cairo_gl_composite_t	*setup,
				     cairo_clip_t		*clip,
				     cairo_traps_t 		*traps)
{
    cairo_int_status_t status;

    cairo_polygon_t polygon;
    cairo_antialias_t antialias;
    cairo_fill_rule_t fill_rule;

    if (! clip)
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (clip->num_boxes == 1 && ! clip->path)
	return _cairo_gl_msaa_compositor_draw_quad (ctx, setup,
						    &clip->boxes[0]);

    if (traps->num_traps == 0) {
	status = _cairo_clip_get_polygon (clip, &polygon, &fill_rule,
					  &antialias);
	if (unlikely (status))
	    return status;

    /* We ignore the antialias mode of the clip here, since the user requested
     * unantialiased rendering of their path and we expect that this stencil
     * based rendering of the clip to be a reasonable approximation to
     * the intersection between that clip and the path.
     *
     * In other words, what the user expects when they try to perform
     * a geometric intersection between an unantialiased polygon and an
     * antialiased polygon is open to interpretation. And we choose the fast
     * option.
     */
	status = _cairo_bentley_ottmann_tessellate_polygon (traps,
							    &polygon,
							    fill_rule);
	_cairo_polygon_fini (&polygon);
	if (unlikely (status))
	    return status;
    }

    status = _draw_traps (ctx, setup, traps);

    return status;
}

static cairo_bool_t
_should_use_unbounded_surface (cairo_composite_rectangles_t *composite)
{
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) composite->surface;
    cairo_rectangle_int_t *source = &composite->source;

    if (composite->is_bounded)
	return FALSE;

    /* This isn't just an optimization. It also detects when painting is used
       to paint back the unbounded surface, preventing infinite recursion. */
    return ! (source->x <= 0 && source->y <= 0 &&
              source->height + source->y >= dst->height &&
              source->width + source->x >= dst->width);
}

static cairo_surface_t*
_prepare_unbounded_surface (cairo_gl_surface_t *dst)
{

    cairo_surface_t* surface = cairo_gl_surface_create (dst->base.device,
							dst->base.content,
							dst->width,
							dst->height);
    if (surface == NULL)
        return NULL;
    if (unlikely (surface->status)) {
        cairo_surface_destroy (surface);
        return NULL;
    }
    return surface;
}

static cairo_int_status_t
_paint_back_unbounded_surface (const cairo_compositor_t		*compositor,
			       cairo_composite_rectangles_t	*composite,
			       cairo_surface_t			*surface)
{
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) composite->surface;
    cairo_int_status_t status;

    cairo_pattern_t *pattern = cairo_pattern_create_for_surface (surface);
    if (unlikely (pattern->status)) {
	status = pattern->status;
	goto finish;
    }

    status = _cairo_compositor_paint (compositor, &dst->base,
				      composite->op, pattern,
				      composite->clip);

finish:
    cairo_pattern_destroy (pattern);
    cairo_surface_destroy (surface);
    return status;
}

static void
_cairo_gl_msaa_compositor_set_clip (cairo_composite_rectangles_t *composite,
				    cairo_gl_composite_t *setup)
{
    uint32_t is_bounded;

    if (_cairo_clip_is_all_clipped (composite->clip))
	return;

    /* We don't need to check CAIRO_OPERATOR_BOUND_BY_MASK in these
       situations. */
    is_bounded = composite->is_bounded;
    composite->is_bounded = CAIRO_OPERATOR_BOUND_BY_SOURCE;
    if (_cairo_composite_rectangles_can_reduce_clip (composite, composite->clip))
	return;

    _cairo_gl_composite_set_clip (setup, composite->clip);

    composite->is_bounded = is_bounded;
}

static void
_gl_pattern_fix_reference_count (const cairo_pattern_t *pattern)
{
   cairo_pattern_type_t pattern_type = cairo_pattern_get_type ((cairo_pattern_t *)pattern);

   /* We need to increase reference count on surface and gradient if
      the original_source_pattern is a cairo_gl_source_t type. */
    if (pattern_type == CAIRO_PATTERN_TYPE_SURFACE) {

	cairo_surface_pattern_t *surface_pattern = (cairo_surface_pattern_t *)pattern;
	cairo_surface_t *pattern_surface = surface_pattern->surface;

	if (cairo_surface_get_type (pattern_surface) == CAIRO_SURFACE_TYPE_GL &&
	    ! pattern_surface->device &&
	    ! _cairo_surface_is_subsurface (pattern_surface)) {

	    cairo_gl_source_t *_source = (cairo_gl_source_t *)pattern_surface;

	    switch (_source->operand.type) {
	    case CAIRO_GL_OPERAND_TEXTURE:
		cairo_surface_reference (&(_source->operand.texture.owns_surface)->base);
		break;
	    case CAIRO_GL_OPERAND_LINEAR_GRADIENT:
	    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_A0:
	    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_NONE:
	    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT:
		_cairo_gl_gradient_reference (_source->operand.gradient.gradient);
		break;
	    default:
	    case CAIRO_GL_OPERAND_NONE:
	    case CAIRO_GL_OPERAND_CONSTANT:
	    case CAIRO_GL_OPERAND_COUNT:
		break;
	    }
	}
    }
}

/* We use two passes to paint with SOURCE operator */
/* The first pass, we use mask as source, to get dst1 = (1 - ma) * dst) with
 * DEST_OUT operator.  In the second pass, we use ADD operator to achieve
 * result = (src * ma) + dst1.  Combining two passes, we have
 * result = (src * ma) + (1 - ma) * dst
 */
static cairo_int_status_t
_cairo_gl_msaa_compositor_mask_source_operator (const cairo_compositor_t *compositor,
						cairo_composite_rectangles_t *composite)
{
    cairo_gl_composite_t setup;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) composite->surface;
    cairo_gl_context_t *ctx = NULL;
    cairo_int_status_t status;
    cairo_traps_t traps;

    _cairo_traps_init (&traps);

    status = _cairo_gl_composite_init (&setup,
				       CAIRO_OPERATOR_DEST_OUT,
				       dst,
				       FALSE /* assume_component_alpha */);
    if (unlikely (status))
	goto finish;

    _gl_pattern_fix_reference_count (composite->original_mask_pattern);

    status = _cairo_gl_composite_set_source (&setup,
					     &composite->mask_pattern.base,
					     &composite->mask_sample_area,
					     &composite->bounded,
					     FALSE);
    if (unlikely (status))
	goto finish;

    status = _cairo_gl_composite_begin_multisample (&setup, &ctx, TRUE);
    if (unlikely (status))
	goto finish;

    if (! composite->clip)
	status = _draw_int_rect (ctx, &setup, &composite->bounded);
    else
	status = _cairo_gl_msaa_compositor_draw_clip (ctx, &setup, composite->clip, &traps);

    _cairo_gl_composite_fini (&setup);
    status = _cairo_gl_context_release (ctx, status);
    ctx = NULL;
    if (unlikely (status))
        return status;

     /* second pass */
    status = _cairo_gl_composite_init (&setup,
				       CAIRO_OPERATOR_ADD,
				       dst,
				       FALSE /* assume_component_alpha */);
    if (unlikely (status))
	goto finish;

    _gl_pattern_fix_reference_count (composite->original_source_pattern);

    status = _cairo_gl_composite_set_source (&setup,
					     &composite->source_pattern.base,
					     &composite->source_sample_area,
					     &composite->bounded,
					     FALSE);
    if (unlikely (status))
	goto finish;

    status = _cairo_gl_composite_set_mask (&setup,
				           &composite->mask_pattern.base,
					   &composite->source_sample_area,
					   &composite->bounded);
    if (unlikely (status))
	goto finish;

    /* We always use multisampling here, because we do not yet have the smarts
       to calculate when the clip or the source requires it. */
    status = _cairo_gl_composite_begin_multisample (&setup, &ctx, TRUE);
    if (unlikely (status))
	goto finish;

    if (! composite->clip)
	status = _draw_int_rect (ctx, &setup, &composite->bounded);
    else
	status = _cairo_gl_msaa_compositor_draw_clip (ctx, &setup, composite->clip, &traps);

finish:
    _cairo_traps_fini (&traps);
    _cairo_gl_composite_fini (&setup);

    if (ctx)
	status = _cairo_gl_context_release (ctx, status);

    return status;
}

static cairo_int_status_t
_cairo_gl_msaa_compositor_mask (const cairo_compositor_t	*compositor,
				cairo_composite_rectangles_t	*composite)
{
    cairo_gl_composite_t setup;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) composite->surface;
    cairo_gl_context_t *ctx = NULL;
    cairo_int_status_t status;
    cairo_operator_t op = composite->op;
    cairo_traps_t traps;
    cairo_bool_t use_color_attribute = FALSE;

    if (should_fall_back (dst, CAIRO_ANTIALIAS_GOOD))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    /* GL compositing operators cannot properly represent a mask operation
       using the SOURCE compositing operator in one pass. This only matters if
       there actually is a mask (there isn't in a paint operation) and if the
       mask isn't totally opaque. */
    if (op == CAIRO_OPERATOR_SOURCE &&
	 composite->original_mask_pattern != NULL &&
	! _cairo_pattern_is_opaque (&composite->mask_pattern.base,
				    &composite->mask_sample_area)) {

       /* If the source is opaque the operation reduces to OVER. */
	if (_cairo_pattern_is_opaque (&composite->source_pattern.base,
				      &composite->source_sample_area))
	    op = CAIRO_OPERATOR_OVER;
	else
	    return _cairo_gl_msaa_compositor_mask_source_operator (compositor, composite);
    }

    if (_should_use_unbounded_surface (composite)) {
	cairo_surface_t* surface = _prepare_unbounded_surface (dst);

	if (unlikely (surface == NULL))
	    return CAIRO_INT_STATUS_UNSUPPORTED;

	/* This may be a paint operation. */
	if (composite->original_mask_pattern == NULL) {
	    status = _cairo_compositor_paint (compositor, surface,
					      CAIRO_OPERATOR_SOURCE,
					      &composite->source_pattern.base,
					      NULL);
	} else {
	    status = _cairo_compositor_mask (compositor, surface,
					     CAIRO_OPERATOR_SOURCE,
					     &composite->source_pattern.base,
					     &composite->mask_pattern.base,
					     NULL);
	}

	if (unlikely (status)) {
	    cairo_surface_destroy (surface);
	    return status;
	}

	return _paint_back_unbounded_surface (compositor, composite, surface);
    }

    status = _cairo_gl_composite_init (&setup,
				       op,
				       dst,
				       FALSE /* assume_component_alpha */);
    if (unlikely (status))
	goto finish;

    _gl_pattern_fix_reference_count (composite->original_source_pattern);

    if (! composite->clip ||
	(composite->clip->num_boxes == 1 && ! composite->clip->path))
	use_color_attribute = TRUE;

    status = _cairo_gl_composite_set_source (&setup,
					     composite->original_source_pattern,
					     &composite->source_sample_area,
					     &composite->bounded,
					     use_color_attribute);
    if (unlikely (status))
	goto finish;

    if (composite->original_mask_pattern != NULL) {
	status = _cairo_gl_composite_set_mask (&setup,
					       composite->original_mask_pattern,
					       &composite->mask_sample_area,
					       &composite->bounded);
    }
    if (unlikely (status))
	goto finish;

    /* We always use multisampling here, because we do not yet have the smarts
       to calculate when the clip or the source requires it. */
    status = _cairo_gl_composite_begin_multisample (&setup, &ctx, TRUE);
    if (unlikely (status))
	goto finish;

    _cairo_traps_init (&traps);

    if (! composite->clip)
	status = _draw_int_rect (ctx, &setup, &composite->bounded);
    else
	status = _cairo_gl_msaa_compositor_draw_clip (ctx, &setup, composite->clip, &traps);

    _cairo_traps_fini (&traps);

finish:
    _cairo_gl_composite_fini (&setup);

    if (ctx)
	status = _cairo_gl_context_release (ctx, status);

    return status;
}

static cairo_int_status_t
_cairo_gl_msaa_compositor_paint (const cairo_compositor_t	*compositor,
				 cairo_composite_rectangles_t	*composite)
{
    return _cairo_gl_msaa_compositor_mask (compositor, composite);
}

static cairo_status_t
_stroke_shaper_add_triangle (void			*closure,
			     const cairo_point_t	 triangle[3])
{
    struct _tristrip_composite_info *info = closure;
    return _cairo_gl_composite_emit_triangle_as_tristrip (info->ctx,
							  &info->setup,
							  triangle);
}

static cairo_status_t
_stroke_shaper_add_triangle_fan (void			*closure,
				 const cairo_point_t	*midpoint,
				 const cairo_point_t	*points,
				 int			 npoints)
{
    struct _tristrip_composite_info *info = closure;
    return _draw_triangle_fan (info->ctx, &info->setup,
			       midpoint, points, npoints);
}

static cairo_status_t
_stroke_shaper_add_quad (void			*closure,
			 const cairo_point_t	 quad[4])
{
    struct _tristrip_composite_info *info = closure;
    return _cairo_gl_composite_emit_quad_as_tristrip (info->ctx, &info->setup,
						      quad);
}

static cairo_int_status_t
_prevent_overlapping_drawing (cairo_gl_context_t 		*ctx,
			      cairo_gl_composite_t 		*setup,
			      cairo_composite_rectangles_t 	*composite,
			      const cairo_path_fixed_t		*path,
			      const cairo_stroke_style_t	*style,
			      const cairo_matrix_t		*ctm)
{
    cairo_rectangle_int_t stroke_extents;
    const cairo_pattern_t *pattern = composite->original_source_pattern;
    cairo_pattern_type_t type = cairo_pattern_get_type ((cairo_pattern_t *) pattern);

    if (! _cairo_gl_ensure_stencil (ctx, setup->dst))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    /* XXX: improve me - since we have lazy init, we cannot use sample
       area */
    if (type == CAIRO_PATTERN_TYPE_SOLID &&
	_cairo_pattern_is_opaque_solid (pattern))
	return CAIRO_INT_STATUS_SUCCESS;

   if (glIsEnabled (GL_STENCIL_TEST) == FALSE) {
       /* In case we have pending operations we have to flush before
	  adding the stencil buffer. */
       _cairo_gl_composite_flush (ctx);

	/* Enable the stencil buffer, even if we are not using it for clipping,
	   so we can use it below to prevent overlapping shapes. We initialize
	   it all to one here which represents infinite clip. */
	glDepthMask (GL_TRUE);
	glEnable (GL_STENCIL_TEST);

	/* If we don't have clip, then we will setup clip extents based on
	   approximate stroke extent. */
	if (! setup->clip) {
	    _cairo_path_fixed_approximate_stroke_extents (path, style, ctm,
							  &stroke_extents);
	    _cairo_gl_scissor_to_extents (setup->dst, &stroke_extents);
	}

	glClearStencil (1);
	glClear (GL_STENCIL_BUFFER_BIT);
	glStencilFunc (GL_EQUAL, 1, 1);
    }

    /* This means that once we draw to a particular pixel nothing else can
       be drawn there until the stencil buffer is reset or the stencil test
       is disabled. */
    glStencilOp (GL_ZERO, GL_ZERO, GL_ZERO);

    /* we need to clean up clip cache */
    _cairo_clip_destroy (ctx->clip);
    ctx->clip = NULL;

    return CAIRO_INT_STATUS_SUCCESS;
}

static void
query_surface_capabilities (cairo_gl_surface_t *surface)
{
    GLint samples, stencil_bits;
    cairo_gl_context_t *ctx;
    cairo_int_status_t status;

    /* Texture surfaces are create in such a way that they always
       have stencil and multisample bits if possible, so we don't
       need to query their capabilities lazily. */
    if (_cairo_gl_surface_is_texture (surface))
	return;
    if (surface->stencil_and_msaa_caps_initialized)
	return;

    surface->stencil_and_msaa_caps_initialized = TRUE;
    surface->supports_stencil = FALSE;
    surface->supports_msaa = FALSE;

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
	return;

    _cairo_gl_context_set_destination (ctx, surface, FALSE);

    glGetIntegerv(GL_SAMPLES, &samples);
    glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
    surface->supports_stencil = stencil_bits > 0;
    surface->supports_msaa = samples > 1;

    status = _cairo_gl_context_release (ctx, status);
}

static cairo_bool_t
should_fall_back (cairo_gl_surface_t *surface,
		  cairo_antialias_t antialias)
{
    query_surface_capabilities (surface);
    if (! surface->supports_stencil)
	return TRUE;

    /* Multisampling OpenGL ES surfaces only maintain one multisampling
       framebuffer and thus must use the spans compositor to do non
       AA rendering */
    if (((cairo_gl_context_t *) surface->base.device)->gl_flavor == CAIRO_GL_FLAVOR_ES
	 && surface->supports_msaa
	 && antialias == CAIRO_ANTIALIAS_NONE)
	return TRUE;

    if (antialias == CAIRO_ANTIALIAS_FAST)
	return TRUE;
    if (antialias == CAIRO_ANTIALIAS_NONE)
	return FALSE;
    return ! surface->supports_msaa;
}

static cairo_int_status_t
_cairo_gl_msaa_compositor_stroke (const cairo_compositor_t	*compositor,
				  cairo_composite_rectangles_t	*composite,
				  const cairo_path_fixed_t	*path,
				  const cairo_stroke_style_t	*style,
				  const cairo_matrix_t		*ctm,
				  const cairo_matrix_t		*ctm_inverse,
				  double			 tolerance,
				  cairo_antialias_t		 antialias)
{
    cairo_int_status_t status;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) composite->surface;
    struct _tristrip_composite_info info;
    cairo_bool_t use_color_attribute;

    if (should_fall_back (dst, antialias))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (composite->is_bounded == FALSE) {
	cairo_surface_t* surface = _prepare_unbounded_surface (dst);

	if (unlikely (surface == NULL))
	    return CAIRO_INT_STATUS_UNSUPPORTED;

	status = _cairo_compositor_stroke (compositor, surface,
					   CAIRO_OPERATOR_SOURCE,
					   &composite->source_pattern.base,
					   path, style, ctm, ctm_inverse,
					   tolerance, antialias, NULL);
	if (unlikely (status)) {
	    cairo_surface_destroy (surface);
	    return status;
	}

	return _paint_back_unbounded_surface (compositor, composite, surface);
    }

    status = _cairo_gl_composite_init (&info.setup,
				       composite->op,
				       dst,
				       FALSE /* assume_component_alpha */);
    if (unlikely (status))
	return status;

    info.ctx = NULL;
    use_color_attribute = _cairo_path_fixed_stroke_is_rectilinear (path) ||
			  _cairo_gl_hairline_style_is_hairline (style, ctm);

    status = _cairo_gl_composite_set_source (&info.setup,
					     composite->original_source_pattern,
					     &composite->source_sample_area,
					     &composite->bounded,
					     use_color_attribute);
    if (unlikely (status))
	goto finish;

    _cairo_gl_msaa_compositor_set_clip (composite, &info.setup);

    status = _cairo_gl_composite_begin_multisample (&info.setup, &info.ctx,
	antialias != CAIRO_ANTIALIAS_NONE);
    if (unlikely (status))
	goto finish;

    if (_cairo_gl_hairline_style_is_hairline (style, ctm)) {
	cairo_gl_hairline_closure_t closure;

	status = _prevent_overlapping_drawing (info.ctx, &info.setup,
					       composite, path, style, ctm);
	if (unlikely (status))
	    goto finish;

	closure.ctx = info.ctx;

	closure.tolerance = tolerance;

	status = _cairo_gl_path_fixed_stroke_to_hairline (path, &closure,
							  style, ctm,
							  ctm_inverse,
							  _cairo_gl_hairline_move_to,
							  style->dash ?
							  _cairo_gl_hairline_line_to_dashed :
							  _cairo_gl_hairline_line_to,
							  _cairo_gl_hairline_curve_to,
							  _cairo_gl_hairline_close_path);
	goto finish;
    }

    if (use_color_attribute || path->has_curve_to) {
	cairo_traps_t traps;

	_cairo_traps_init (&traps);

	status = _cairo_path_fixed_stroke_to_traps (path, style,
						    ctm, ctm_inverse,
						    tolerance, &traps);
	if (unlikely (status)) {
	    _cairo_traps_fini (&traps);
	    goto finish;
	}

	status = _draw_traps (info.ctx, &info.setup, &traps);
	_cairo_traps_fini (&traps);
    } else {
	status = _prevent_overlapping_drawing (info.ctx, &info.setup,
					       composite, path, style, ctm);
	if (unlikely (status))
	    goto finish;

	status =
	    _cairo_path_fixed_stroke_to_shaper ((cairo_path_fixed_t *) path,
						style,
						ctm,
						ctm_inverse,
						tolerance,
						_stroke_shaper_add_triangle,
						_stroke_shaper_add_triangle_fan,
						_stroke_shaper_add_quad,
						&info);
	if (unlikely (status))
	    goto finish;
    }
finish:
    _cairo_gl_composite_fini (&info.setup);

    if (info.ctx)
	status = _cairo_gl_context_release (info.ctx, status);

    return status;
}

static cairo_int_status_t
_cairo_gl_msaa_compositor_fill_rectilinear (const cairo_compositor_t *compositor,
					    cairo_composite_rectangles_t *composite,
					    const cairo_path_fixed_t *path,
					    cairo_fill_rule_t fill_rule,
					    double tolerance,
					    cairo_antialias_t antialias,
					    cairo_clip_t *clip)
{
    cairo_gl_composite_t setup;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) composite->surface;
    cairo_gl_context_t *ctx = NULL;
    cairo_int_status_t status;
    int i;

    status = _cairo_gl_composite_init (&setup,
				       composite->op,
				       dst,
				       FALSE /* assume_component_alpha */);
    if (unlikely (status))
	goto cleanup_setup;

    status = _cairo_gl_composite_set_source (&setup,
					     composite->original_source_pattern,
					     &composite->source_sample_area,
					     &composite->bounded,
					     TRUE);
    if (unlikely (status))
	goto cleanup_setup;

    status = _cairo_gl_composite_begin_multisample (&setup, &ctx,
	antialias != CAIRO_ANTIALIAS_NONE);
    if (unlikely (status))
	goto cleanup_setup;

    for (i = 0; i < clip->num_boxes; i++) {
	status = _cairo_gl_msaa_compositor_draw_quad (ctx, &setup,
						      &clip->boxes[i]);
	if (unlikely (status))
	    goto cleanup_setup;
    }

cleanup_setup:
    _cairo_gl_composite_fini (&setup);

    if (ctx)
	status = _cairo_gl_context_release (ctx, status);

    return status;
}

static cairo_int_status_t
_cairo_gl_msaa_compositor_fill (const cairo_compositor_t	*compositor,
				cairo_composite_rectangles_t	*composite,
				const cairo_path_fixed_t	*path,
				cairo_fill_rule_t		 fill_rule,
				double				 tolerance,
				cairo_antialias_t		 antialias)
{
    cairo_gl_composite_t setup;
    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) composite->surface;
    cairo_gl_context_t *ctx = NULL;
    cairo_int_status_t status = CAIRO_INT_STATUS_SUCCESS;
    cairo_traps_t traps;
    cairo_bool_t use_color_attr = FALSE;

    if (should_fall_back (dst, antialias))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (composite->is_bounded == FALSE) {
	cairo_surface_t* surface = _prepare_unbounded_surface (dst);

	if (unlikely (surface == NULL))
	    return CAIRO_INT_STATUS_UNSUPPORTED;


	status = _cairo_compositor_fill (compositor, surface,
					 CAIRO_OPERATOR_SOURCE,
					 &composite->source_pattern.base,
					 path, fill_rule, tolerance,
					 antialias, NULL);

	if (unlikely (status)) {
	    cairo_surface_destroy (surface);
	    return status;
	}

	return _paint_back_unbounded_surface (compositor, composite, surface);
    }

    if (_cairo_path_fixed_fill_is_rectilinear (path) &&
	composite->clip != NULL &&
	composite->clip->num_boxes == 1 &&
	composite->clip->path == NULL) {
	cairo_clip_t *clip = _cairo_clip_copy (composite->clip);
	clip = _cairo_clip_intersect_rectilinear_path (clip,
						       path,
						       fill_rule,
						       antialias);
	if (clip->num_boxes)
		status = _cairo_gl_msaa_compositor_fill_rectilinear (compositor,
								     composite,
								     path,
								     fill_rule,
								     tolerance,
								     antialias,
								     clip);
	_cairo_clip_destroy (clip);

	return status;
    }

    status = _cairo_gl_composite_init (&setup,
				       composite->op,
				       dst,
				       FALSE /* assume_component_alpha */);
    if (unlikely (status)) {
        _cairo_gl_composite_fini (&setup);
	return status;
    }

    _cairo_traps_init (&traps);

    if (_cairo_path_fixed_fill_is_rectilinear (path)) {
	status = _cairo_path_fixed_fill_rectilinear_to_traps (path,
							      fill_rule,
							      antialias,
							      &traps);
	use_color_attr = TRUE;
    } else
	status = _cairo_path_fixed_fill_to_traps (path, fill_rule,
						  tolerance, &traps);
    if (unlikely (status))
	goto cleanup_traps;

    status = _cairo_gl_composite_set_source (&setup,
					     composite->original_source_pattern,
					     &composite->source_sample_area,
					     &composite->bounded,
					     use_color_attr);
    if (unlikely (status))
	goto cleanup_setup;

    _cairo_gl_msaa_compositor_set_clip (composite, &setup);

    status = _cairo_gl_composite_begin_multisample (&setup, &ctx,
	antialias != CAIRO_ANTIALIAS_NONE);
    if (unlikely (status))
	goto cleanup_setup;

    status = _draw_traps (ctx, &setup, &traps);
    if (unlikely (status))
        goto cleanup_setup;

cleanup_setup:
    _cairo_gl_composite_fini (&setup);

    if (ctx)
	status = _cairo_gl_context_release (ctx, status);

cleanup_traps:
    _cairo_traps_fini (&traps);

    return status;
}

static cairo_int_status_t
_cairo_gl_msaa_compositor_glyphs (const cairo_compositor_t	*compositor,
				  cairo_composite_rectangles_t	*composite,
				  cairo_scaled_font_t		*scaled_font,
				  cairo_glyph_t			*glyphs,
				  int				 num_glyphs,
				  cairo_bool_t			 overlap)
{
    cairo_int_status_t status;
    cairo_surface_t *src = NULL;
    int src_x, src_y;
    cairo_composite_glyphs_info_t info;

    cairo_gl_surface_t *dst = (cairo_gl_surface_t *) composite->surface;

    query_surface_capabilities (dst);
    if (! dst->supports_stencil)
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (composite->is_bounded == FALSE) {
	cairo_surface_t* surface = _prepare_unbounded_surface (dst);

	if (unlikely (surface == NULL))
	    return CAIRO_INT_STATUS_UNSUPPORTED;

	status = _cairo_compositor_glyphs (compositor, surface,
					   CAIRO_OPERATOR_SOURCE,
					   &composite->source_pattern.base,
					   glyphs, num_glyphs,
					   scaled_font, composite->clip);

	if (unlikely (status)) {
	    cairo_surface_destroy (surface);
	    return status;
	}

	return _paint_back_unbounded_surface (compositor, composite, surface);
    }

    src = _cairo_gl_pattern_to_source (&dst->base,
				       composite->original_source_pattern,
				       FALSE,
				       &composite->bounded,
				       &composite->source_sample_area,
				       &src_x, &src_y);
    if (unlikely (src->status)) {
	status = src->status;
	goto finish;
    }

    status = _cairo_gl_check_composite_glyphs (composite,
					       scaled_font, glyphs,
					       &num_glyphs);
    if (unlikely (status != CAIRO_INT_STATUS_SUCCESS))
	goto finish;

    info.font = scaled_font;
    info.glyphs = glyphs;
    info.num_glyphs = num_glyphs;
    info.use_mask = overlap || ! composite->is_bounded ||
		    composite->op == CAIRO_OPERATOR_SOURCE;
    info.extents = composite->bounded;

    _cairo_scaled_font_freeze_cache (scaled_font);
    status = _cairo_gl_composite_glyphs_with_clip (dst, composite->op,
						   src, src_x, src_y,
						   0, 0, &info,
						   composite->clip,
						   TRUE);

    _cairo_scaled_font_thaw_cache (scaled_font);

finish:
    if (src)
	cairo_surface_destroy (src);

    return status;
}

static void
_cairo_gl_msaa_compositor_init (cairo_compositor_t	 *compositor,
				const cairo_compositor_t *delegate)
{
    compositor->delegate = delegate;
    compositor->lazy_init = TRUE;

    compositor->paint = _cairo_gl_msaa_compositor_paint;
    compositor->mask = _cairo_gl_msaa_compositor_mask;
    compositor->fill = _cairo_gl_msaa_compositor_fill;
    compositor->stroke = _cairo_gl_msaa_compositor_stroke;
    compositor->glyphs = _cairo_gl_msaa_compositor_glyphs;
}

const cairo_compositor_t *
_cairo_gl_msaa_compositor_get (void)
{
    static cairo_compositor_t compositor;
    if (compositor.delegate == NULL)
	_cairo_gl_msaa_compositor_init (&compositor,
					_cairo_gl_span_compositor_get ());

    return &compositor;
}
