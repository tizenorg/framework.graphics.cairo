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

#ifndef __TIZEN_CAIRO_EVAS_GL_DOC_H__
#define __TIZEN_CAIRO_EVAS_GL_DOC_H__

/**
 * @defgroup CAPI_CAIRO_EVAS_GL_MODULE Cairo GL
 * @brief  Cairo GL/Evas_GL APIs are offical APIs for Tizen.
 * @ingroup OPENSRC_CAIRO_FRAMEWORK
 *
 * @section CAIRO_EVAS_GL Required Header
 *   \#include <cairo-evas-gl.h>
 *
 * @section CAIRO_EVAS_GL_MODULE_OVERVIEW Overview
 * In Tizen, Cairo provides gl backend in order to do hardware-accelerated rendering.
 * Since the EGL is not public supported in Tizen, Cairo Evas_GL has been provided to user interfaces instead to allow indirect access to EGL layer.
 *
 *
 * Features	:\n
 *	- Support a new cairo_device structure for interface to the underlying GL or EvasGL.\n
 *	- Support a new cairo_surface structure for representing GL or Evas_GL_Surface object that cairo can render to.\n
 *	- Get the underlying Evas_GL object used to create cairo device object.\n
 *	- Get the underlying Evas_GL_Context object used to create cairo device object.
 *
 * Remarks	:\n
 *	- Cairo GL and Cairo Evas_GL will use an GL/Evas_GL context and API set.\n
 *	- Therefore, Evas_GL and OpenGL-ES should be provided for normal operation of Cairo gl backend.\n
 */

#endif // __TIZEN_CAIRO_EVAS_GL_DOC_H__
