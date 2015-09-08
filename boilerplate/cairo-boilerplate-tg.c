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

#include "cairo-boilerplate-private.h"

#include <cairo-tg.h>
#include <assert.h>

static cairo_surface_t *
_cairo_boilerplate_tg_create_surface (const char		*name,
				      cairo_content_t		content,
				      double			width,
				      double			height,
				      double			max_width,
				      double			max_height,
				      cairo_boilerplate_mode_t	mode,
				      void			**closure)
{
    cairo_format_t format;

    if (content == CAIRO_CONTENT_COLOR_ALPHA)
    {
	format = CAIRO_FORMAT_ARGB32;
    }
    else if (content == CAIRO_CONTENT_COLOR)
    {
	format = CAIRO_FORMAT_RGB24;
    }
    else
    {
	assert (0);
	return NULL;
    }

    *closure = NULL;

    return cairo_tg_surface_create (format, ceil (width), ceil (height));
}

static const cairo_boilerplate_target_t targets[] =
{
    {
	"tg", "tg", NULL, NULL,
	CAIRO_SURFACE_TYPE_TG, CAIRO_CONTENT_COLOR_ALPHA, 0,
	NULL,
	_cairo_boilerplate_tg_create_surface,
	cairo_surface_create_similar,
	NULL, NULL,
	_cairo_boilerplate_get_image_surface,
	cairo_surface_write_to_png,
	NULL, NULL, NULL,
	TRUE, FALSE, FALSE
    }
};
CAIRO_BOILERPLATE (tg, targets)
