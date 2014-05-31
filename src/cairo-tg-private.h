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

#ifndef CAIRO_TG_PRIVATE_H
#define CAIRO_TG_PRIVATE_H

#include "cairo-default-context-private.h"
#include "cairo-surface-private.h"
#include "cairo-tg-journal-private.h"
#include <pixman.h>

#define CAIRO_TG_NUM_MAX_TILES	8

typedef struct _cairo_tg_surface
{
    cairo_surface_t	    base;

    cairo_format_t	    format;
    pixman_format_code_t    pixman_format;
    unsigned char	    *data;
    int			    width;
    int			    height;
    int			    stride;
    int			    bpp;

    cairo_surface_t	    *image_surface;
    cairo_surface_t	    *tile_surfaces[CAIRO_TG_NUM_MAX_TILES];
    cairo_tg_journal_t	    journal;
} cairo_tg_surface_t;

#endif /* CAIRO_TG_PRIVATE_H */
