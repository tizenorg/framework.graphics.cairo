/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Chris Wilson
 * Copyright © 2014 Samsung Research America, Inc - Silicon Valley
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
 * The Initial Developer of the Original Code is Henry Song.
 */

#include "cairo-boilerplate-private.h"

#include <cairo-gl.h>
#include <cairo-evas-gl.h>
#include <Ecore_Evas.h>
#include <Ecore.h>
#include <Evas_GL.h>

static const cairo_user_data_key_t gl_closure_key;

typedef struct _evas_gl_target_closure {
    Evas_GL *evas_gl;
    Evas_GL_Context *evas_ctx;
    Evas_GL_API *evas_api;

    cairo_device_t *device;
    cairo_surface_t *surface;
} evas_gl_target_closure_t;

static void
_cairo_boilerplate_evas_gl_cleanup (void *closure)
{
    evas_gl_target_closure_t *gltc = closure;

    cairo_device_finish (gltc->device);
    cairo_device_destroy (gltc->device);

    evas_gl_context_destroy (gltc->evas_gl, gltc->evas_ctx);
    evas_gl_free (gltc->evas_gl);

    free (gltc);

    ecore_evas_shutdown ();
    ecore_shutdown ();
}

static cairo_surface_t *
_cairo_boilerplate_evas_gl_create_surface (const char		 *name,
					   cairo_content_t	  content,
					   double		  width,
					   double		  height,
					   double		  max_width,
					   double		  max_height,
					   cairo_boilerplate_mode_t   mode,
					   void			**closure)
{
    Ecore_Evas *ee;
    Evas *canvas;

    evas_gl_target_closure_t *gltc;
    cairo_surface_t *surface;

    if (width < 1)
	width = 1;
    if (height < 1)
	height = 1;

    ecore_init ();
    ecore_evas_init ();
    ee = ecore_evas_gl_x11_new (NULL, 0, 0, 0, ceil (width), ceil (height));;
    canvas = ecore_evas_get (ee);

    gltc = xcalloc (1, sizeof (evas_gl_target_closure_t));
    *closure = gltc;

    gltc->evas_gl = evas_gl_new (canvas);
    gltc->evas_ctx = evas_gl_context_create (gltc->evas_gl, NULL);
    gltc->evas_api = evas_gl_api_get (gltc->evas_gl);

    gltc->device = cairo_evas_gl_device_create (gltc->evas_gl, gltc->evas_ctx);

    gltc->surface = surface =
	cairo_gl_surface_create (gltc->device, CAIRO_CONTENT_COLOR_ALPHA,
					     ceil (width), ceil (height));
    if (cairo_surface_status (surface))
	_cairo_boilerplate_evas_gl_cleanup (gltc);

    return surface;
}

static void
_cairo_boilerplate_evas_gl_synchronize (void *closure)
{
    evas_gl_target_closure_t *gltc = closure;

    if (cairo_device_acquire (gltc->device))
	return;

    gltc->evas_api->glFinish ();

    cairo_device_release (gltc->device);
}

static const cairo_boilerplate_target_t targets[] = {
    {
	"evasgl", "gl", NULL, NULL,
	CAIRO_SURFACE_TYPE_GL, CAIRO_CONTENT_COLOR_ALPHA, 1,
	"cairo_evas_gl_device_create",
	_cairo_boilerplate_evas_gl_create_surface,
	cairo_surface_create_similar,
	NULL, NULL,
	_cairo_boilerplate_get_image_surface,
	cairo_surface_write_to_png,
	_cairo_boilerplate_evas_gl_cleanup,
	_cairo_boilerplate_evas_gl_synchronize,
        NULL,
	TRUE, FALSE, FALSE
    }
};
CAIRO_BOILERPLATE (evasgl, targets)
