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

#ifndef CAIRO_THREAD_LOCAL_PRIVATE_H
#define CAIRO_THREAD_LOCAL_PRIVATE_H

#if CAIRO_HAS_TLS || CAIRO_HAS_PTHREAD_SETSPECIFIC
#define CAIRO_HAS_THREAD_LOCAL 1
#endif

#if CAIRO_HAS_THREAD_LOCAL
#if defined(TLS)

#   define CAIRO_DEFINE_THREAD_LOCAL(type, name)			\
    static TLS type name

#   define CAIRO_GET_THREAD_LOCAL(name)					\
    (&name)

#elif defined(__MINGW32__)

#   define _NO_W32_PSEUDO_MODIFIERS
#   include <windows.h>

#   define CAIRO_DEFINE_THREAD_LOCAL(type, name)			\
    static volatile int tls_ ## name ## _initialized = 0;		\
    static void *tls_ ## name ## _mutex = NULL;				\
    static unsigned tls_ ## name ## _index;				\
									\
    static type *							\
    tls_ ## name ## _alloc (void)					\
    {									\
        type *value = calloc (1, sizeof (type));			\
        if (value)							\
            TlsSetValue (tls_ ## name ## _index, value);		\
        return value;							\
    }									\
									\
    static inline type *						\
    tls_ ## name ## _get (void)						\
    {									\
	type *value;							\
	if (!tls_ ## name ## _initialized)				\
	{								\
	    if (!tls_ ## name ## _mutex)				\
	    {								\
		void *mutex = CreateMutexA (NULL, 0, NULL);		\
		if (InterlockedCompareExchangePointer (			\
			&tls_ ## name ## _mutex, mutex, NULL) != NULL)	\
		{							\
		    CloseHandle (mutex);				\
		}							\
	    }								\
	    WaitForSingleObject (tls_ ## name ## _mutex, 0xFFFFFFFF);	\
	    if (!tls_ ## name ## _initialized)				\
	    {								\
		tls_ ## name ## _index = TlsAlloc ();			\
		tls_ ## name ## _initialized = 1;			\
	    }								\
	    ReleaseMutex (tls_ ## name ## _mutex);			\
	}								\
	if (tls_ ## name ## _index == 0xFFFFFFFF)			\
	    return NULL;						\
	value = TlsGetValue (tls_ ## name ## _index);			\
	if (!value)							\
	    value = tls_ ## name ## _alloc ();				\
	return value;							\
    }

#   define CAIRO_GET_THREAD_LOCAL(name)					\
    tls_ ## name ## _get ()

#elif defined(_MSC_VER)

#   define CAIRO_DEFINE_THREAD_LOCAL(type, name)			\
    static __declspec(thread) type name

#   define CAIRO_GET_THREAD_LOCAL(name)					\
    (&name)

#elif defined(CAIRO_HAS_PTHREAD_SETSPECIFIC)

#include <pthread.h>

#  define CAIRO_DEFINE_THREAD_LOCAL(type, name)				\
    static pthread_once_t tls_ ## name ## _once_control = PTHREAD_ONCE_INIT; \
    static pthread_key_t tls_ ## name ## _key;				\
									\
    static void								\
    tls_ ## name ## _destroy_value (void *value)			\
    {									\
	free (value);							\
    }									\
									\
    static void								\
    tls_ ## name ## _make_key (void)					\
    {									\
	pthread_key_create (&tls_ ## name ## _key,			\
			    tls_ ## name ## _destroy_value);		\
    }									\
									\
    static type *							\
    tls_ ## name ## _alloc (void)					\
    {									\
	type *value = calloc (1, sizeof (type));			\
	if (value)							\
	    pthread_setspecific (tls_ ## name ## _key, value);		\
	return value;							\
    }									\
									\
    static inline type *						\
    tls_ ## name ## _get (void)						\
    {									\
	type *value = NULL;						\
	if (pthread_once (&tls_ ## name ## _once_control,		\
			  tls_ ## name ## _make_key) == 0)		\
	{								\
	    value = pthread_getspecific (tls_ ## name ## _key);		\
	    if (!value)							\
		value = tls_ ## name ## _alloc ();			\
	}								\
	return value;							\
    }

#   define CAIRO_GET_THREAD_LOCAL(name)					\
    tls_ ## name ## _get ()

#else

#    error "Unknown thread local support for this system."

#endif
#endif /* CAIRO_HAS_THREAD_LOCAL */

#endif /* CAIRO_THREAD_LOCAL_PRIVATE_H */
