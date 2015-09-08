/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
 * Copyright © 2005 Red Hat, Inc
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
 * The Initial Developer of the Original Code is Red Hat, Inc.
 *
 * Contributor(s):
 *	Carl Worth <cworth@cworth.org>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 */

#include "cairoint.h"

#include "cairo-gl-private.h"

#include "cairo-error-private.h"

#if CAIRO_HAS_EVASGL_SURFACE && CAIRO_HAS_GLESV2_SURFACE
extern void glActiveTexture (GLenum texture);
extern void glBindTexture (GLenum target, GLuint texture);
extern void glBlendFunc (GLenum sfactor, GLenum dfactor);
extern void glBlendFuncSeparate (GLenum srcRGB, GLenum dstRGB,
			         GLenum srcAlpha, GLenum dstAlpha);
extern void glClear (GLbitfield mask);
extern void glClearColor (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern void glClearStencil (GLint s);
extern void glColorMask (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
extern void glDeleteTextures (GLsizei n, const GLuint *textures);
extern void glDepthMask (GLboolean flag);
extern void glDisable (GLenum cap);
extern void glDrawArrays (GLenum mode, GLint first, GLsizei count);
extern void glDrawElements (GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
extern void glEnable (GLenum cap);
extern void glFlush (void);
extern void glGenTextures (GLsizei n, GLuint *textures);
extern void glGetBooleanv (GLenum pname, GLboolean *data);
extern GLenum glGetError (void);
extern void glGetFloatv (GLenum pname, GLfloat *data);
extern void glGetIntegerv (GLenum pname, GLint *data);
extern const unsigned char* glGetString (GLenum pname);
extern void glPixelStorei (GLenum pname, GLint param);
extern void glReadPixels (GLint x, GLint y, GLsizei width, GLsizei height,
			  GLenum format, GLenum type, GLvoid *data);
extern void glScissor (GLint x, GLint y, GLsizei width, GLsizei height);
extern void glStencilFunc (GLenum func, GLint ref, GLuint mask);
extern void glStencilMask (GLuint mask);
extern void glStencilOp (GLenum sfail, GLenum dpfail, GLenum dppass);
extern void glTexSubImage2D (GLenum target, GLint level,
			     GLint xoffset, GLint yoffset,
			     GLsizei width, GLsizei height,
			     GLenum format, GLenum type, const GLvoid *data);
extern void glTexImage2D (GLenum target, GLint level, GLenum internalformat,
			  GLsizei width, GLsizei height,
			  GLint border, GLenum format,
			  GLenum type, const GLvoid *data);
extern void glTexParameteri (GLenum target, GLenum pname, GLint param);
extern void glViewport (GLint x, GLint y, GLsizei width, GLsizei height);
#endif

typedef struct _cairo_egl_context {
    cairo_gl_context_t base;

    EGLDisplay display;
    EGLContext context;

    EGLSurface dummy_surface;
    EGLSurface current_surface;

    EGLDisplay previous_display;
    EGLContext previous_context;
    EGLSurface previous_surface;
} cairo_egl_context_t;

typedef struct _cairo_egl_surface {
    cairo_gl_surface_t base;

    EGLSurface egl;
} cairo_egl_surface_t;


static cairo_bool_t
_context_acquisition_changed_egl_state (cairo_egl_context_t *ctx,
					EGLSurface current_surface)
{
    return ctx->previous_context != ctx->context ||
           ctx->previous_surface != current_surface ||
           ctx->previous_display != ctx->display;
}

static EGLSurface
_egl_get_current_surface (cairo_egl_context_t *ctx)
{
    if (ctx->base.current_target == NULL ||
        _cairo_gl_surface_is_texture (ctx->base.current_target)) {
	return  ctx->dummy_surface;
    }

    return ((cairo_egl_surface_t *) ctx->base.current_target)->egl;
}

static void
_egl_query_current_state (cairo_egl_context_t *ctx)
{
    ctx->previous_surface = eglGetCurrentSurface (EGL_DRAW);
    ctx->previous_context = eglGetCurrentContext ();
    ctx->previous_display = eglGetCurrentDisplay ();

    /* If any of the values were none, assume they are all none. Not all
       drivers seem well behaved when it comes to using these values across
       multiple threads. */
    if (ctx->previous_surface == EGL_NO_SURFACE ||
        ctx->previous_context == EGL_NO_CONTEXT || ctx->previous_display == EGL_NO_DISPLAY) {
        ctx->previous_surface = EGL_NO_SURFACE;
        ctx->previous_context = EGL_NO_CONTEXT;
        ctx->previous_display = EGL_NO_DISPLAY;
    }
}

static void
_egl_acquire (void *abstract_ctx)
{
    cairo_egl_context_t *ctx = abstract_ctx;
    EGLSurface current_surface = _egl_get_current_surface (ctx);

    _egl_query_current_state (ctx);
    if (!_context_acquisition_changed_egl_state (ctx, current_surface))
	return;

    _cairo_gl_context_reset (&ctx->base);
    eglMakeCurrent (ctx->display,
		    current_surface, current_surface, ctx->context);

    ctx->current_surface = current_surface;
}

static void
_egl_release (void *abstract_ctx)
{
    cairo_egl_context_t *ctx = abstract_ctx;
    if (!ctx->base.thread_aware ||
	!_context_acquisition_changed_egl_state (ctx,
						 _egl_get_current_surface (ctx))) {
	return;
    }

    eglMakeCurrent (ctx->display,
		    EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    ctx->current_surface = EGL_NO_SURFACE;
}

static void
_egl_make_current (void *abstract_ctx,
	           cairo_gl_surface_t *abstract_surface)
{
    cairo_egl_context_t *ctx = abstract_ctx;
    cairo_egl_surface_t *surface = (cairo_egl_surface_t *) abstract_surface;

    if (surface->egl != ctx->current_surface) {
	eglMakeCurrent(ctx->display, surface->egl, surface->egl, ctx->context);
	ctx->current_surface = surface->egl;
    }
}

static void
_egl_swap_buffers (void *abstract_ctx,
		   cairo_gl_surface_t *abstract_surface)
{
    cairo_egl_context_t *ctx = abstract_ctx;
    cairo_egl_surface_t *surface = (cairo_egl_surface_t *) abstract_surface;

    eglSwapBuffers (ctx->display, surface->egl);
}

static void
_egl_destroy (void *abstract_ctx)
{
    cairo_egl_context_t *ctx = abstract_ctx;

    eglMakeCurrent (ctx->display,
		    EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (ctx->dummy_surface != EGL_NO_SURFACE)
        eglDestroySurface (ctx->display, ctx->dummy_surface);
}

static cairo_bool_t
_egl_make_current_surfaceless(cairo_egl_context_t *ctx)
{
    const char *extensions;

    extensions = eglQueryString(ctx->display, EGL_EXTENSIONS);
    if (extensions == NULL)
	return FALSE;

    if (strstr(extensions, "EGL_KHR_surfaceless_context") == NULL &&
	strstr(extensions, "EGL_KHR_surfaceless_opengl") == NULL)
	return FALSE;

    if (!eglMakeCurrent(ctx->display,
			EGL_NO_SURFACE, EGL_NO_SURFACE, ctx->context))
	return FALSE;

    return TRUE;
}

static cairo_gl_generic_func_t
_cairo_egl_get_proc_address (void *data, const char *name)
{
    int i;
    struct {
	cairo_gl_generic_func_t func;
	const char *name;
    } func_map[] = {
    { (cairo_gl_generic_func_t)glActiveTexture,	"glActiveTexture"	},
    { (cairo_gl_generic_func_t)glBindTexture,	"glBindTexture"		},
    { (cairo_gl_generic_func_t)glBlendFunc,	"glBlendFunc"		},
    { (cairo_gl_generic_func_t)glBlendFuncSeparate,"glBlendFuncSeparate"},
    { (cairo_gl_generic_func_t)glClear,		"glClear"		},
    { (cairo_gl_generic_func_t)glClearColor,	"glClearColor"		},
    { (cairo_gl_generic_func_t)glClearStencil,	"glClearStencil"	},
    { (cairo_gl_generic_func_t)glColorMask,	"glColorMask"		},
    { (cairo_gl_generic_func_t)glDeleteTextures,"glDeleteTextures"	},
    { (cairo_gl_generic_func_t)glDepthMask,	"glDepthMask"		},
    { (cairo_gl_generic_func_t)glDisable,	"glDisable"		},
    { (cairo_gl_generic_func_t)glDrawArrays,	"glDrawArrays"		},
    { (cairo_gl_generic_func_t)glDrawElements,	"glDrawElements"	},
    { (cairo_gl_generic_func_t)glEnable,	"glEnable"		},
    { (cairo_gl_generic_func_t)glGenTextures,	"glGenTextures"		},
    { (cairo_gl_generic_func_t)glGetBooleanv,	"glGetBooleanv"		},
    { (cairo_gl_generic_func_t)glGetError,	"glGetError"		},
    { (cairo_gl_generic_func_t)glGetFloatv,	"glGetFloatv"		},
    { (cairo_gl_generic_func_t)glGetIntegerv,	"glGetIntegerv"		},
    { (cairo_gl_generic_func_t)glGetString,	"glGetString"		},
    { (cairo_gl_generic_func_t)glFlush,		"glFlush"		},
    { (cairo_gl_generic_func_t)glPixelStorei,	"glPixelStorei"		},
    { (cairo_gl_generic_func_t)glReadPixels,	"glReadPixels"		},
    { (cairo_gl_generic_func_t)glScissor,	"glScissor"		},
    { (cairo_gl_generic_func_t)glStencilFunc,	"glStencilFunc"		},
    { (cairo_gl_generic_func_t)glStencilMask,	"glStencilMask"		},
    { (cairo_gl_generic_func_t)glStencilOp,	"glStencilOp"		},
    { (cairo_gl_generic_func_t)glTexImage2D,	"glTexImage2D"		},
    { (cairo_gl_generic_func_t)glTexSubImage2D,	"glTexSubImage2D"	},
    { (cairo_gl_generic_func_t)glTexParameteri,	"glTexParameteri"	},
    { (cairo_gl_generic_func_t)glViewport,	"glViewport"		},
    { NULL,					 NULL			}
    };

    for (i = 0; func_map[i].name; i++) {
	if (! strncmp (func_map[i].name, name, strlen(name)))
	    return func_map[i].func;
    }

    return eglGetProcAddress (name);
}

cairo_device_t *
cairo_egl_device_create (EGLDisplay dpy, EGLContext egl)
{
    cairo_egl_context_t *ctx;
    cairo_status_t status;
    int attribs[] = {
	EGL_WIDTH, 1,
	EGL_HEIGHT, 1,
	EGL_NONE,
    };
    EGLConfig config;
    EGLint numConfigs;

    ctx = calloc (1, sizeof (cairo_egl_context_t));
    if (unlikely (ctx == NULL))
	return _cairo_gl_context_create_in_error (CAIRO_STATUS_NO_MEMORY);

    ctx->display = dpy;
    ctx->context = egl;

    ctx->base.acquire = _egl_acquire;
    ctx->base.release = _egl_release;
    ctx->base.make_current = _egl_make_current;
    ctx->base.swap_buffers = _egl_swap_buffers;
    ctx->base.destroy = _egl_destroy;

    /* We are about the change the current state of EGL, so we should
     * query the pre-existing surface now instead of later. */
    _egl_query_current_state (ctx);

    if (!_egl_make_current_surfaceless (ctx)) {
	/* Fall back to dummy surface, meh. */
	EGLint config_attribs[] = {
	    EGL_CONFIG_ID, 0,
	    EGL_NONE
	};

	/*
	 * In order to be able to make an egl context current when using a
	 * pbuffer surface, that surface must have been created with a config
	 * that is compatible with the context config. For Mesa, this means
	 * that the configs must be the same.
	 */
	eglQueryContext (dpy, egl, EGL_CONFIG_ID, &config_attribs[1]);
	eglChooseConfig (dpy, config_attribs, &config, 1, &numConfigs);

	ctx->dummy_surface = eglCreatePbufferSurface (dpy, config, attribs);
	if (ctx->dummy_surface == NULL) {
	    free (ctx);
	    return _cairo_gl_context_create_in_error (CAIRO_STATUS_NO_MEMORY);
	}

	if (!eglMakeCurrent (dpy, ctx->dummy_surface, ctx->dummy_surface, egl)) {
	    free (ctx);
	    return _cairo_gl_context_create_in_error (CAIRO_STATUS_NO_MEMORY);
	}
    }

    status = _cairo_gl_dispatch_init (&ctx->base.dispatch,
				      _cairo_egl_get_proc_address, NULL);
    if (unlikely (status)) {
	free (ctx);
	return _cairo_gl_context_create_in_error (status);
    }

    status = _cairo_gl_context_init (&ctx->base);
    if (unlikely (status)) {
	if (ctx->dummy_surface != EGL_NO_SURFACE)
	    eglDestroySurface (dpy, ctx->dummy_surface);
	free (ctx);
	return _cairo_gl_context_create_in_error (status);
    }

    eglMakeCurrent (dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    ctx->current_surface = EGL_NO_SURFACE;

    return &ctx->base.base;
}

cairo_surface_t *
cairo_gl_surface_create_for_egl (cairo_device_t	*device,
				 EGLSurface	 egl,
				 int		 width,
				 int		 height)
{
    cairo_egl_surface_t *surface;

    if (unlikely (device->status))
	return _cairo_surface_create_in_error (device->status);

    if (device->backend->type != CAIRO_DEVICE_TYPE_GL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));

    if (width <= 0 || height <= 0)
        return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_SIZE));

    surface = calloc (1, sizeof (cairo_egl_surface_t));
    if (unlikely (surface == NULL))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    _cairo_gl_surface_init (device, &surface->base,
			    CAIRO_CONTENT_COLOR_ALPHA, width, height);
    surface->egl = egl;

    return &surface->base.base;
}

static cairo_bool_t is_egl_device (cairo_device_t *device)
{
    return (device->backend != NULL &&
	    device->backend->type == CAIRO_DEVICE_TYPE_GL);
}

static cairo_egl_context_t *to_egl_context (cairo_device_t *device)
{
    return (cairo_egl_context_t *) device;
}

EGLDisplay
cairo_egl_device_get_display (cairo_device_t *device)
{
    if (! is_egl_device (device)) {
	_cairo_error_throw (CAIRO_STATUS_DEVICE_TYPE_MISMATCH);
	return EGL_NO_DISPLAY;
    }

    return to_egl_context (device)->display;
}

cairo_public EGLContext
cairo_egl_device_get_context (cairo_device_t *device)
{
    if (! is_egl_device (device)) {
	_cairo_error_throw (CAIRO_STATUS_DEVICE_TYPE_MISMATCH);
	return EGL_NO_CONTEXT;
    }

    return to_egl_context (device)->context;
}
