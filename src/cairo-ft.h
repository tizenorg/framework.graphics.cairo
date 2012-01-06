/* cairo - a vector graphics library with display and print output
 *
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
 *      Graydon Hoare <graydon@redhat.com>
 *	Owen Taylor <otaylor@redhat.com>
 */

#ifndef CAIRO_FT_H
#define CAIRO_FT_H

#include "cairo.h"

#if CAIRO_HAS_FT_FONT

/* Fontconfig/Freetype platform-specific font interface */

#include <ft2build.h>
#include FT_FREETYPE_H

#if CAIRO_HAS_FC_FONT
#include <fontconfig/fontconfig.h>
#endif

CAIRO_BEGIN_DECLS

cairo_public cairo_font_face_t *
cairo_ft_font_face_create_for_ft_face (FT_Face         face,
				       int             load_flags);

cairo_public FT_Face
cairo_ft_scaled_font_lock_face (cairo_scaled_font_t *scaled_font);

cairo_public void
cairo_ft_scaled_font_unlock_face (cairo_scaled_font_t *scaled_font);

#if CAIRO_HAS_FC_FONT

cairo_public cairo_font_face_t *
cairo_ft_font_face_create_for_pattern (FcPattern *pattern);

cairo_public void
cairo_ft_font_options_substitute (const cairo_font_options_t *options,
				  FcPattern                  *pattern);

#endif

#ifndef TIZEN_ENABLE_SET_EXTRA_FLAGS
/* For support to set extra_flags from application */
#define TIZEN_ENABLE_SET_EXTRA_FLAGS
#endif

#ifdef TIZEN_ENABLE_SET_EXTRA_FLAGS
/* This enum is moved from cairo-ft-font.c to cairo-ft.h for support to set extra_flags from application */
typedef enum _cairo_ft_extra_flags {
    CAIRO_FT_OPTIONS_HINT_METRICS = (1 << 0),
    CAIRO_FT_OPTIONS_EMBOLDEN = (1 << 1)
} cairo_ft_extra_flags_t;

/* Set extra_flags to cairo_ft_font_face_t's ft_options from application */
cairo_public void cairo_ft_font_option_set_extra_flags (cairo_font_face_t * font_face,
				cairo_ft_extra_flags_t extra_flags);
#endif

CAIRO_END_DECLS

#else  /* CAIRO_HAS_FT_FONT */
# error Cairo was not compiled with support for the freetype font backend
#endif /* CAIRO_HAS_FT_FONT */

#endif /* CAIRO_FT_H */
