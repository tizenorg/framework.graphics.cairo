/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Chris Wilson
 * Copyright © 2014 Samsung Electronics
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
 */


#ifdef CAIRO_HAS_TTRACE
#include <ttrace.h>
//#define CAIRO_TRACE_BEGIN(NAME) traceBegin(TTRACE_TAG_GRAPHICS, NAME)
//#define CAIRO_TRACE_END() traceEnd(TTRACE_TAG_GRAPHICS)
#define CAIRO_TRACE_BEGIN(NAME) traceAsyncBegin (TTRACE_TAG_GRAPHICS, 0, NAME);
#define CAIRO_TRACE_END(NAME) traceAsyncEnd(TTRACE_TAG_GRAPHICS, 0, NAME);
#else
#define CAIRO_TRACE_BEGIN(NAME)
#define CAIRO_TRACE_END(NAME)
#define CAIRO_TRACE_ASYNC_BEGIN(NAME, KEY)
#define CAIRO_TRACE_ASYNC_END(NAME, KEY)
#endif


