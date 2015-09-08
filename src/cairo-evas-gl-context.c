/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
 * Copyright © 2005 Red Hat, Inc
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
   You should have received a copy of the LGPL along with this library
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
 * The Initial Developer of the Original Code is Red Hat, Inc.
 *
 * Contributor(s):
 *	Carl Worth <cworth@cworth.org>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 *	Henry Song <henry.song@samsung.com>
 */

#include "cairoint.h"
#include "cairo-gl-private.h"
#include "cairo-error-private.h"
#if CAIRO_HAS_DLSYM
#include <dlfcn.h>
#endif

#include "cairo-evas-gl.h"

typedef struct _cairo_evas_gl_context {
    cairo_gl_context_t base;

    Evas_GL *evas_gl;
    Evas_GL_Surface *surface;
    Evas_GL_Context *context;

    Evas_GL_Surface *dummy_surface;
    Evas_GL_Surface *current_surface;

    Evas_GL_Context *queried_context;

    cairo_bool_t has_multithread_makecurrent;
} cairo_evas_gl_context_t;

typedef struct _cairo_evas_gl_surface {
    cairo_gl_surface_t base;

    Evas_GL_Surface *surface;
} cairo_evas_gl_surface_t;

/* as of efl-1.12.0, the following GL functions are supported, for other
   GL functions and extensions, we us dlsym to get.

   The following APIs are not supported by efl-1.12
   glDrawBuffer, glReadBuffer, glMapBuffer, glUnmapBuffer,
   glBlitFramebuffer{ANGLE}, glRenderbufferStorageMultisample{ANGLE},

   This functions are queried via dlsym.  cairo does not use map/unmap
   buffer.  For rest unsupported GL functions, they should not change
   evas_gl internal context states (I hope).
 */
static cairo_gl_generic_func_t
_cairo_evas_gl_get_proc_addr (void *data, const char *name)
{
    static Evas_GL_API *api;
    Evas_GL *gl = (Evas_GL *) data;
    int i;

    struct {
	size_t func;
	const char *name;
    } evas_gl_func_map[] = {
	/* core function, glReadBuffer and glDrawBuffer not supported */
	{ offsetof (Evas_GL_API, glActiveTexture	), "glActiveTexture" },
	{ offsetof (Evas_GL_API, glBindTexture		), "glBindTexture" },
	{ offsetof (Evas_GL_API, glBlendFunc		), "glBlendFunc" },
	{ offsetof (Evas_GL_API, glBlendFuncSeparate	), "glBlendFuncSeparate" },
	{ offsetof (Evas_GL_API, glClear		), "glClear" },
	{ offsetof (Evas_GL_API, glClearColor		), "glClearColor" },
	{ offsetof (Evas_GL_API, glClearStencil		), "glClearStencil" },
	{ offsetof (Evas_GL_API, glColorMask		), "glColorMask" },
	{ offsetof (Evas_GL_API, glDeleteTextures	), "glDeleteTextures" },
	{ offsetof (Evas_GL_API, glDepthMask		), "glDepthMask" },
	{ offsetof (Evas_GL_API, glDisable		), "glDisable" },
	{ offsetof (Evas_GL_API, glDrawArrays		), "glDrawArrays" },
	{ offsetof (Evas_GL_API, glDrawElements		), "glDrawElements" },
	{ offsetof (Evas_GL_API, glEnable		), "glEnable" },
	{ offsetof (Evas_GL_API, glGenTextures		), "glGenTextures" },
	{ offsetof (Evas_GL_API, glGetBooleanv		), "glGetBooleanv" },
	{ offsetof (Evas_GL_API, glGetError		), "glGetError" },
	{ offsetof (Evas_GL_API, glGetFloatv		), "glGetFloatv" },
	{ offsetof (Evas_GL_API, glGetIntegerv		), "glGetIntegerv" },
	{ offsetof (Evas_GL_API, glGetString		), "glGetString" },
	{ offsetof (Evas_GL_API, glPixelStorei		), "glPixelStorei" },
	{ offsetof (Evas_GL_API, glReadPixels		), "glReadPixels" },
	{ offsetof (Evas_GL_API, glScissor		), "glScissor" },
	{ offsetof (Evas_GL_API, glStencilFunc		), "glStencilFunc" },
	{ offsetof (Evas_GL_API, glStencilMask		), "glStencilMask" },
	{ offsetof (Evas_GL_API, glStencilOp		), "glStencilOp" },
	{ offsetof (Evas_GL_API, glTexImage2D		), "glTexImage2D" },
	{ offsetof (Evas_GL_API, glTexSubImage2D	), "glTexSubImage2D" },
	{ offsetof (Evas_GL_API, glTexParameteri	), "glTexParameteri" },
	{ offsetof (Evas_GL_API, glViewport		), "glViewport" },
	/* buffers functions, glMapBufferOES, glUnmapBufferOES not
	   suported
	 */
	{ offsetof (Evas_GL_API, glGenBuffers		), "glGenBuffers" },
	{ offsetof (Evas_GL_API, glBindBuffer		), "glBindBuffer" },
	{ offsetof (Evas_GL_API, glBufferData		), "glBufferData" },
	/* shader functions */
	{ offsetof (Evas_GL_API, glCreateShader		), "glCreateShader" },
	{ offsetof (Evas_GL_API, glShaderSource		), "glShaderSource" },
	{ offsetof (Evas_GL_API, glCompileShader	), "glCompileShader" },
	{ offsetof (Evas_GL_API, glGetShaderiv		), "glGetShaderiv" },
	{ offsetof (Evas_GL_API, glGetShaderInfoLog	), "glGetShaderInfoLog" },
	{ offsetof (Evas_GL_API, glDeleteShader		), "glDeleteShader" },
	/* program functions */
	{ offsetof (Evas_GL_API, glCreateProgram	), "glCreateProgram" },
	{ offsetof (Evas_GL_API, glAttachShader		), "glAttachShader" },
	{ offsetof (Evas_GL_API, glDeleteProgram	), "glDeleteProgram" },
	{ offsetof (Evas_GL_API, glLinkProgram		), "glLinkProgram" },
	{ offsetof (Evas_GL_API, glUseProgram		), "glUseProgram" },
	{ offsetof (Evas_GL_API, glGetProgramiv		), "glGetProgramiv" },
	{ offsetof (Evas_GL_API, glGetProgramInfoLog	), "glGetProgramInfoLog" },
	/* uniform functions */
	{ offsetof (Evas_GL_API, glGetUniformLocation	), "glGetUniformLocation" },
	{ offsetof (Evas_GL_API, glUniform1f		), "glUniform1f" },
	{ offsetof (Evas_GL_API, glUniform2f		), "glUniform2f" },
	{ offsetof (Evas_GL_API, glUniform3f		), "glUniform3f" },
	{ offsetof (Evas_GL_API, glUniform4f		), "glUniform4f" },
	{ offsetof (Evas_GL_API, glUniform1fv		), "glUniform1fv" },
	{ offsetof (Evas_GL_API, glUniformMatrix3fv	), "glUniformMatrix3fv" },
	{ offsetof (Evas_GL_API, glUniformMatrix4fv	), "glUniformMatrix4fv" },
	{ offsetof (Evas_GL_API, glUniform1i		), "glUniform1i" },
	/* attribute functions */
	{ offsetof (Evas_GL_API, glBindAttribLocation	), "glBindAttribLocation" },
	{ offsetof (Evas_GL_API, glVertexAttribPointer	), "glVertexAttribPointer" },
	{ offsetof (Evas_GL_API, glEnableVertexAttribArray), "glEnableVertexAttribArray" },
	{ offsetof (Evas_GL_API, glDisableVertexAttribArray), "glDisableVertexAttribArray" },
	/* fbo functions */
	{ offsetof (Evas_GL_API, glGenFramebuffers	), "glGenFramebuffers" },
	{ offsetof (Evas_GL_API, glBindFramebuffer	), "glBindFramebuffer" },
	{ offsetof (Evas_GL_API, glFramebufferTexture2D	), "glFramebufferTexture2D" },
	{ offsetof (Evas_GL_API, glCheckFramebufferStatus), "glCheckFramebufferStatus" },
	{ offsetof (Evas_GL_API, glDeleteFramebuffers	), "glDeleteFramebuffers" },
	{ offsetof (Evas_GL_API, glGenRenderbuffers	), "glGenRenderbuffers" },
	{ offsetof (Evas_GL_API, glBindRenderbuffer	), "glBindRenderbuffer" },
	{ offsetof (Evas_GL_API, glRenderbufferStorage	), "glRenderbufferStorage" },
	{ offsetof (Evas_GL_API, glFramebufferRenderbuffer), "glFramebufferRenderbuffer" },
	{ offsetof (Evas_GL_API, glDeleteRenderbuffers	), "glDeleteRenderbuffers" },
	/* multisampling functions, none supported in efl-1.12.0 */
	{ 0, NULL }
    };

    api = evas_gl_api_get (gl);

    for (i = 0; evas_gl_func_map[i].name; i++) {
	if (! strncmp (evas_gl_func_map[i].name, name, strlen(name)))
	    return *((cairo_gl_generic_func_t *) (((char *) &api->version) + evas_gl_func_map[i].func));
    }

    return evas_gl_proc_address_get (gl, name);
}

static cairo_bool_t
_context_acquisition_changed_evas_gl_state (cairo_evas_gl_context_t *ctx,
					    Evas_GL_Surface *current_surface)
{
    return ctx->queried_context != ctx->context ||
	   ctx->current_surface != current_surface;
}

static Evas_GL_Surface *
_evas_gl_get_current_surface (cairo_evas_gl_context_t *ctx)
{
    if (ctx->base.current_target == NULL ||
        _cairo_gl_surface_is_texture (ctx->base.current_target)) {
	return  ctx->dummy_surface;
    }

    return ((cairo_evas_gl_surface_t *) ctx->base.current_target)->surface;
}

static void
_evas_gl_query_current_state (cairo_evas_gl_context_t *ctx)
{
	ctx->queried_context = evas_gl_current_context_get (ctx->evas_gl);
	ctx->current_surface = evas_gl_current_surface_get (ctx->evas_gl);
}

static void
_evas_gl_acquire (void *abstract_ctx)
{
    cairo_evas_gl_context_t *ctx = abstract_ctx;
    Evas_GL_Surface *current_surface = _evas_gl_get_current_surface (ctx);

    _evas_gl_query_current_state (ctx);
    if (!_context_acquisition_changed_evas_gl_state (ctx, current_surface))
	return;

    _cairo_gl_context_reset (&ctx->base);
    evas_gl_make_current (ctx->evas_gl, current_surface, ctx->context);

    ctx->current_surface = current_surface;
    //ctx->base.current_target = &ctx->dummy_surface->base;
}

static void
_evas_gl_release (void *abstract_ctx)
{
    cairo_evas_gl_context_t *ctx = abstract_ctx;
    if (!ctx->base.thread_aware || ctx->has_multithread_makecurrent ||
	!_context_acquisition_changed_evas_gl_state (ctx,
						 _evas_gl_get_current_surface (ctx))) {
	return;
    }

    _cairo_gl_composite_flush (&ctx->base);
    evas_gl_make_current (ctx->evas_gl, NULL, NULL);
    ctx->current_surface = NULL;
}

static void
_evas_gl_make_current (void *abstract_ctx,
	           cairo_gl_surface_t *abstract_surface)
{
    cairo_evas_gl_context_t *ctx = abstract_ctx;
    cairo_evas_gl_surface_t *surface = (cairo_evas_gl_surface_t *) abstract_surface;

    if (surface->surface != ctx->current_surface) {
	evas_gl_make_current (ctx->evas_gl, surface->surface, ctx->context);
	ctx->current_surface = surface->surface;
    }
}

static void
_evas_gl_swap_buffers (void *abstract_ctx,
		   cairo_gl_surface_t *abstract_surface)
{ }

static void
_evas_gl_destroy (void *abstract_ctx)
{
    cairo_evas_gl_context_t *ctx = abstract_ctx;

    evas_gl_make_current (ctx->evas_gl, NULL, NULL);
    if (! ctx->dummy_surface)
	evas_gl_surface_destroy (ctx->evas_gl, ctx->dummy_surface);
}

cairo_device_t *
cairo_evas_gl_device_create (Evas_GL *evas_gl,
			     Evas_GL_Context *evas_context)
{
    Evas_GL_Config *evas_cfg;
    cairo_evas_gl_context_t *ctx;
    cairo_status_t status;

	if (! evas_gl ||! evas_context) {
		fprintf (stderr, "cairo_evas_gl_device_create(): evas_gl or evas_context is NULL\n");
	return _cairo_gl_context_create_in_error (CAIRO_STATUS_NULL_POINTER);
	}

    ctx = calloc (1, sizeof (cairo_evas_gl_context_t));
    if (unlikely (ctx == NULL))
	return _cairo_gl_context_create_in_error (CAIRO_STATUS_NO_MEMORY);

    ctx->evas_gl = evas_gl;
    ctx->context = evas_context;
    evas_cfg = evas_gl_config_new ();

    ctx->base.acquire = _evas_gl_acquire;
    ctx->base.release = _evas_gl_release;
    ctx->base.make_current = _evas_gl_make_current;
    ctx->base.swap_buffers = _evas_gl_swap_buffers;
    ctx->base.destroy = _evas_gl_destroy;

    /* We are about the change the current state of evas-gl, so we should
     * query the pre-existing surface now instead of later. */
    _evas_gl_query_current_state (ctx);

    ctx->dummy_surface = evas_gl_pbuffer_surface_create (ctx->evas_gl,
							 evas_cfg,
							 1, 1, NULL);
    //evas_gl_config_free (evas_cfg);

    if (ctx->dummy_surface == NULL) {
        free (ctx);
	return _cairo_gl_context_create_in_error (CAIRO_STATUS_NO_MEMORY);
    }

    if (!evas_gl_make_current (ctx->evas_gl, ctx->dummy_surface, evas_context)) {
	evas_gl_surface_destroy (ctx->evas_gl, ctx->dummy_surface);
	free (ctx);
	return _cairo_gl_context_create_in_error (CAIRO_STATUS_NO_MEMORY);
    }

    status = _cairo_gl_dispatch_init (&ctx->base.dispatch,
				      _cairo_evas_gl_get_proc_addr,
				      ctx->evas_gl);
    if (unlikely (status)) {
	evas_gl_surface_destroy (ctx->evas_gl, ctx->dummy_surface);
	free (ctx);
	return _cairo_gl_context_create_in_error (status);
    }

    status = _cairo_gl_context_init (&ctx->base);
    if (unlikely (status)) {
	evas_gl_surface_destroy (ctx->evas_gl, ctx->dummy_surface);
	free (ctx);
	return _cairo_gl_context_create_in_error (status);
    }

    if (strstr(evas_gl_string_query (ctx->evas_gl, EVAS_GL_EXTENSIONS),
				     "GLX_MESA_multithread_makecurrent"))
	ctx->has_multithread_makecurrent = TRUE;
    else
	ctx->has_multithread_makecurrent = FALSE;

    evas_gl_make_current (ctx->evas_gl, NULL, NULL);
    return &ctx->base.base;
}

cairo_surface_t *
cairo_gl_surface_create_for_evas_gl (cairo_device_t	*device,
				     Evas_GL_Surface	*evas_surface,
				     Evas_GL_Config     *evas_config,
				     int		 width,
				     int		 height)
{
    cairo_evas_gl_surface_t *surface;

	if ((! device)||(cairo_device_status(device)!= CAIRO_STATUS_SUCCESS)){
		fprintf (stderr, "cairo_gl_surface_create_for_evas_gl(): cairo device is NULL or not available\n");
	return _cairo_surface_create_in_error(CAIRO_STATUS_NULL_POINTER);
	}

    if (unlikely (device->status))
	return _cairo_surface_create_in_error (device->status);

	if (! evas_surface || ! evas_config) {
		fprintf (stderr, "cairo_gl_surface_create_for_evas_gl(): evas_surface or evas_config is NULL\n");
	return _cairo_surface_create_in_error (CAIRO_STATUS_NULL_POINTER);
	}

    if (device->backend->type != CAIRO_DEVICE_TYPE_GL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));

    if (width <= 0 || height <= 0)
        return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_SIZE));

    surface = calloc (1, sizeof (cairo_evas_gl_surface_t));
    if (unlikely (surface == NULL))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    _cairo_gl_surface_init (device, &surface->base,
			    CAIRO_CONTENT_COLOR_ALPHA, width, height);

    if (evas_config->stencil_bits != EVAS_GL_STENCIL_NONE)
	surface->base.supports_stencil = TRUE;

    /* does not matter num of samples */
    if (evas_config->multisample_bits != EVAS_GL_MULTISAMPLE_NONE) {
	surface->base.num_samples = 2;
	surface->base.supports_msaa = TRUE;
    }

    surface->base.stencil_and_msaa_caps_initialized = TRUE;

    surface->surface = evas_surface;

    return &surface->base.base;
}

static cairo_bool_t is_evas_gl_device (cairo_device_t *device)
{
		return (device->backend != NULL &&
	    device->backend->type == CAIRO_DEVICE_TYPE_GL);
}

static cairo_evas_gl_context_t *to_evas_gl_context (cairo_device_t *device)
{
	return (cairo_evas_gl_context_t *) device;
}

cairo_public Evas_GL *
cairo_evas_gl_device_get_gl (cairo_device_t *device)
{
	if ((! device)||(cairo_device_status(device)!= CAIRO_STATUS_SUCCESS)){
		fprintf(stderr,"\n cairo_evas_gl_device_get_gl(): cairo device is NULL or not available");
	_cairo_error_throw(CAIRO_STATUS_DEVICE_ERROR);
	return NULL;
	}

	if (! is_evas_gl_device (device)) {
	_cairo_error_throw (CAIRO_STATUS_DEVICE_TYPE_MISMATCH);
	return NULL;
    }

    return to_evas_gl_context (device)->evas_gl;
}

cairo_public Evas_GL_Context *
cairo_evas_gl_device_get_context (cairo_device_t *device)
{
	if ((! device)||(cairo_device_status(device)!= CAIRO_STATUS_SUCCESS)){
		fprintf(stderr,"\n cairo_evas_gl_device_get_context(): cairo device is NULL or not available");		
	_cairo_error_throw (CAIRO_STATUS_DEVICE_ERROR);
	return NULL;
	}

    if (! is_evas_gl_device (device)) {
	_cairo_error_throw (CAIRO_STATUS_DEVICE_TYPE_MISMATCH);
	return NULL;
    }

    return to_evas_gl_context (device)->context;
}
