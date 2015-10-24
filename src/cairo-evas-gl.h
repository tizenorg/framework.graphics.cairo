/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
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
 * The Initial Developer of the Original Code is Eric Anholt.
 */

#ifndef CAIRO_EVAS_GL_H
#define CAIRO_EVAS_GL_H

#include "cairo.h"

#if CAIRO_HAS_EVASGL_SURFACE

CAIRO_BEGIN_DECLS

#include <Evas_GL.h>

/**
 * @addtogroup CAPI_CAIRO_EVAS_GL_MODULE
 * @{
 */

/* Evas_GL.h does not define GLchar */
typedef char GLchar;
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER	0x8CA9
#endif

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER	0x8CA8
#endif

#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT		0x402
#endif

#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL	0x84F9
#endif

#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8	0x88F0
#endif

#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES		0x8D57
#endif

#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT	0x821A
#endif

/* Cairo-gl API, which is Open-source, can be used at the Cairo_EvasGL backend */

/**
 * @brief Create a cairo GL surface using the device as the underlying rendering system.
 *
 * @since_tizen 2.3.1
 *
 * @param[in] device         The given cairo_device_t represents the driver interface to underlying rendering system
 * @param[in] content	      Type of content in the surface
 * @param[in] width          Width of the surface, in pixels
 * @param[in] height         Height of the surface, in pixels
 *
 * @return The created surface or NULL on failure
 *	The error value on failure can be retrieved with cairo_status().
 */

cairo_public cairo_surface_t *
cairo_gl_surface_create (cairo_device_t *device,
			 cairo_content_t content,
			 int width, int height);

/**
 * @brief Create a cairo GL surface using the texture as the render target, and the device as the underlying rendering system.\n
 * 	The content must match the format of the texture\n
 * 	CAIRO_CONTENT_ALPHA <-> GL_ALPHA\n
 * 	CAIRO_CONTENT_COLOR <-> GL_RGB/GL_BGR\n
 * 	CAIRO_CONTENT_COLOR_ALPHA <-> GL_RGBA/GL_BGRA\n
 *
 * @since_tizen 2.3.1
 *
 * @param[in] abstract_device         The given cairo_device_t represents the driver interface to underlying rendering system
 * @param[in] content	     Type of content in the surface
 * @param[in] tex              Name of texture to use for storage of surface pixels
 * @param[in] width           Width of the surface, in pixels
 * @param[in] height          Height of the surface, in pixels
 *
 * @return The created surface or NULL on failure
 *	The error value on failure can be retrieved with cairo_status().
 */

cairo_public cairo_surface_t *
cairo_gl_surface_create_for_texture (cairo_device_t *abstract_device,
				     cairo_content_t content,
				     unsigned int tex,
                                     int width, int height);

/**
 * @brief Returns the width of given cairo surface object
 *
 * @since_tizen 2.3.1
 *
 * @param[in] abstract_surface        The given cairo_surface_t object
 *
 * @return the surface width or 0 on failure
 *	The error value on failure can be retrieved with cairo_status().
 */

cairo_public int
cairo_gl_surface_get_width (cairo_surface_t *abstract_surface);

/**
 * @brief Returns the height of given cairo surface object
 *
 * @since_tizen 2.3.1
 *
 * @param[in] abstract_surface         The given cairo_surface_t object
 *
 * @return the surface height or 0 on failure
 *	The error value on failure can be retrieved with cairo_status().
 */

cairo_public int
cairo_gl_surface_get_height (cairo_surface_t *abstract_surface);

/**
 * @brief Cairo can be used in multithreaded environment.\n
 * 	By default, cairo switches out the current GL context after each draw finishes.\n
 * 	This API tells cairo not to switch GL context if no other thread uses cairo for rendering.\n
 * 	In carefully designed application, there should be a single, dedicated rendering thread.\n
 *
 * @since_tizen 2.3.1
 *
 * @param[in] device         The given cairo_device structure for the interface to underlying rendering system
 * @param[in] thread_aware       Set this value as FALSE to choose non-thread-aware mode
 */

cairo_public void
cairo_gl_device_set_thread_aware (cairo_device_t	*device,
				  cairo_bool_t		 thread_aware);

/**
 * @brief Creates and returns a new cairo_device structure for interface to underlying rendering system.
 *
 * @since_tizen 2.3.1
 *
 * @param[in] evas_gl        The given Evas_gl object
 * @param[in] evas_context   The given Evas GL Context object
 *
 * @return The created cairo_device structure, or an error status on failure.
 * 	The error value can be retrieved with cairo_device_status().
 *
 * @see evas_gl_new
 * @see evas_gl_context_create
 * @see cairo_gl_surface_create_for_evas_gl
 */

cairo_public cairo_device_t *
cairo_evas_gl_device_create (Evas_GL		*evas_gl,
			     Evas_GL_Context	*evas_context);

/**
 * @brief Creates and returns a new cairo_surface structure for representing Evas_GL_Surface object that cairo can render to.
 *
 * @since_tizen 2.3.1
 *
 * @param[in] device         The given cairo_device structure for the interface to underlying rendering system
 * @param[in] evas_surface   The given Evas_GL_Surface object for GL Rendering
 * @param[in] evas_config       The pixel format and configuration of the rendering surface
 * @param[in] width          The width of the surface
 * @param[in] height         The height of the surface
 *
 * @return The created cairo_surface structure, or an error status on failure
 * 	The error value can be retrieved with cairo_surface_status().
 *
 * @see cairo_evas_gl_device_create
 * @see evas_gl_surface_create
 * @see evas_gl_config_new
 */

cairo_public cairo_surface_t *
cairo_gl_surface_create_for_evas_gl (cairo_device_t	*device,
				     Evas_GL_Surface	*evas_surface,
				     Evas_GL_Config     *evas_config,
				     int		 width,
				     int		 height);

/**
 * @brief Returns the underlying Evas_GL object used to create cairo device object
 *
 * @since_tizen 2.3.1
 *
 * @param[in] device         The given cairo_device_t represents the driver interface to underlying rendering system
 *
 * @return The created Evas_GL object or NULL on failure
 *	The error value on failure can be retrieved with cairo_status().
 */

cairo_public Evas_GL *
cairo_evas_gl_device_get_gl (cairo_device_t *device);

/**
 * @brief Returns the underlying Evas_GL_Context object used to create cairo device object.
 *
 * @since_tizen 2.3.1
 *
 * @param[in] device         The given cairo_device_t represents the driver interface to underlying rendering system
 *
 * @return The created Evas_GL_Context object or NULL on failure
 *	The error value on failure can be retrieved with cairo_status().
 */

cairo_public Evas_GL_Context *
cairo_evas_gl_device_get_context (cairo_device_t *device);

/**
 * @}
 */

CAIRO_END_DECLS
#else  /* CAIRO_HAS_EVASGL_SURFACE */
# error Cairo was not compiled with support for the Evas GL backend
#endif /* CAIRO_HAS_EVASGL_SURFACE */

#endif /* CAIRO_EVAS_GL_H */
