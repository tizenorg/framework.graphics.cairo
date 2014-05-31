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

#include "cairoint.h"
#include "cairo-surface-fallback-private.h"
#include "cairo-tg.h"
#include "cairo-tg-private.h"
#include "cairo-image-surface-inline.h"
#include "cairo-surface-subsurface-inline.h"
#include "cairo-compositor-private.h"
#include "cairo-clip-inline.h"
#include "cairo-recording-surface-inline.h"

#if CAIRO_HAS_OPENMP
#include <omp.h>
#endif

#define	CAIRO_TG_THREAD_POOL_BUSY_WAIT
#define CAIRO_TG_NUM_MIN_ENTRIES_FOR_PARALLEL_FLUSH 2

static inline int
get_num_cpu_cores (void)
{
    static int num_cpu_cores = 0;

    if (num_cpu_cores == 0)
    {
#if CAIRO_HAS_OPENMP
	num_cpu_cores = omp_get_num_procs ();
#elif defined (__WIN32)
	SYSTEM_INFO sysinfo;

	GetSystemInfo (&sysinfo);
	num_cpu_cores = sysinfo.dwNumberOfProcessors;
#elif defined (__linux__)
	cpu_set_t   cs;
	int	    i;

	CPU_ZERO (&cs);

	if (sched_getaffinity (0, sizeof (cs), &cs) != 0)
	    num_cpu_cores = 1;

	for (i = 0; i < 8; i++)
	{
	    if (CPU_ISSET (i, &cs))
		num_cpu_cores++;
	}
#else
	num_cpu_cores = 1;
#endif
    }

    return num_cpu_cores;
}

static inline int
get_bpp_for_format (cairo_format_t format)
{
    switch (format)
    {
    case CAIRO_FORMAT_ARGB32:
    case CAIRO_FORMAT_RGB24:
    case CAIRO_FORMAT_RGB30:
	return 32;
    case CAIRO_FORMAT_RGB16_565:
	return 16;
    case CAIRO_FORMAT_A8:
	return 8;
    case CAIRO_FORMAT_A1:
	return 1;
    case CAIRO_FORMAT_INVALID:
    default:
	ASSERT_NOT_REACHED;
	return 0;
    }
}

static inline cairo_bool_t
_cairo_surface_is_tg(const cairo_surface_t *surface)
{
    return surface->backend && surface->backend->type == CAIRO_SURFACE_TYPE_TG;
}

static inline cairo_bool_t
_cairo_tg_surface_is_size_valid (int width, int height)
{
    if (width < 0 || height < 0)
	return FALSE;

    /* TODO: Check for upper limit of surface size. */

    return TRUE;
}

static inline cairo_bool_t
_cairo_pattern_is_self_copy (cairo_surface_t	    *surface,
			     const cairo_pattern_t  *pattern)
{
    if (unlikely (surface == NULL))
	return FALSE;

    if (unlikely (pattern == NULL))
	return FALSE;

    if (pattern->type == CAIRO_PATTERN_TYPE_SURFACE )
    {
	cairo_surface_t *pattern_surface =
	    ((cairo_surface_pattern_t *) pattern)->surface;

	while (_cairo_surface_is_subsurface (pattern_surface))
	{
	    pattern_surface =
		_cairo_surface_subsurface_get_target (pattern_surface);
	}

	return pattern_surface == surface;
    }

    return FALSE;
}

static inline cairo_bool_t
_cairo_pattern_is_recording (const cairo_pattern_t *pattern)
{
    cairo_surface_t *surface;

    if (pattern->type != CAIRO_PATTERN_TYPE_SURFACE)
	return FALSE;

    surface = ((const cairo_surface_pattern_t *) pattern)->surface;
    return _cairo_surface_is_recording (surface);
}

static inline cairo_bool_t
_cairo_tg_surface_owns_data (cairo_tg_surface_t *surface)
{
    return ((cairo_image_surface_t *) surface->image_surface)->owns_data;
}

static inline cairo_int_status_t
_cairo_tg_image_surface_paint (void			*closure,
			       cairo_operator_t		op,
			       const cairo_pattern_t	*source,
			       const cairo_clip_t	*clip)
{
    cairo_image_surface_t   *surface = (cairo_image_surface_t *) closure;
    cairo_int_status_t status;

    status = _cairo_surface_begin_modification (&surface->base);

    if (unlikely (status))
	return status;

    status = _cairo_compositor_paint (surface->compositor, &surface->base,
				      op, source, clip);

    if (status != CAIRO_INT_STATUS_NOTHING_TO_DO)
    {
	surface->base.is_clear = op == CAIRO_OPERATOR_CLEAR && clip == NULL;
	surface->base.serial++;
    }

    return status;
}

static inline cairo_int_status_t
_cairo_tg_image_surface_mask (void			*closure,
			      cairo_operator_t		op,
			      const cairo_pattern_t	*source,
			      const cairo_pattern_t	*mask,
			      const cairo_clip_t	*clip)
{
    cairo_image_surface_t   *surface = (cairo_image_surface_t *) closure;
    cairo_int_status_t status;

    status = _cairo_surface_begin_modification (&surface->base);

    if (unlikely (status))
	return status;

    status = _cairo_compositor_mask (surface->compositor, &surface->base,
				     op, source, mask, clip);

    if (status != CAIRO_INT_STATUS_NOTHING_TO_DO)
    {
	surface->base.is_clear = FALSE;
	surface->base.serial++;
    }

    return status;
}

static inline cairo_int_status_t
_cairo_tg_image_surface_stroke (void			    *closure,
				cairo_operator_t	    op,
				const cairo_pattern_t	    *source,
				const cairo_path_fixed_t    *path,
				const cairo_stroke_style_t  *style,
				const cairo_matrix_t	    *ctm,
				const cairo_matrix_t	    *ctm_inverse,
				double			    tolerance,
				cairo_antialias_t	    antialias,
				const cairo_clip_t	    *clip)
{
    cairo_image_surface_t   *surface = (cairo_image_surface_t *) closure;
    cairo_int_status_t status;

    status = _cairo_surface_begin_modification (&surface->base);

    if (unlikely (status))
	return status;

    status =  _cairo_compositor_stroke (surface->compositor, &surface->base,
					op, source, path,
					style, ctm, ctm_inverse,
					tolerance, antialias, clip);

    if (status != CAIRO_INT_STATUS_NOTHING_TO_DO)
    {
	surface->base.is_clear = FALSE;
	surface->base.serial++;
    }

    return status;
}


static inline cairo_int_status_t
_cairo_tg_image_surface_fill (void			*closure,
			      cairo_operator_t		op,
			      const cairo_pattern_t	*source,
			      const cairo_path_fixed_t	*path,
			      cairo_fill_rule_t		fill_rule,
			      double			tolerance,
			      cairo_antialias_t		antialias,
			      const cairo_clip_t	*clip)
{
    cairo_image_surface_t   *surface = (cairo_image_surface_t *) closure;
    cairo_int_status_t status;

    status = _cairo_surface_begin_modification (&surface->base);

    if (unlikely (status))
	return status;

    status = _cairo_compositor_fill (surface->compositor, &surface->base,
				     op, source, path,
				     fill_rule, tolerance, antialias, clip);

    if (status != CAIRO_INT_STATUS_NOTHING_TO_DO)
    {
	surface->base.is_clear = FALSE;
	surface->base.serial++;
    }

    return status;
}

static inline cairo_int_status_t
_cairo_tg_image_surface_glyphs (void			*closure,
				cairo_operator_t	op,
				const cairo_pattern_t	*source,
				cairo_glyph_t		*glyphs,
				int			num_glyphs,
				cairo_scaled_font_t	*scaled_font,
				const cairo_clip_t	*clip)
{
    cairo_image_surface_t   *surface = (cairo_image_surface_t *) closure;
    cairo_int_status_t status;

    status = _cairo_surface_begin_modification (&surface->base);

    if (unlikely (status))
	return status;

    status = _cairo_compositor_glyphs (surface->compositor, &surface->base,
				       op, source,
				       glyphs, num_glyphs, scaled_font,
				       clip);

    if (status != CAIRO_INT_STATUS_NOTHING_TO_DO)
    {
	surface->base.is_clear = FALSE;
	surface->base.serial++;
    }

    return status;
}

const cairo_tg_journal_replay_funcs_t replay_funcs_image_fallback =
{
    _cairo_tg_image_surface_paint,
    _cairo_tg_image_surface_mask,
    _cairo_tg_image_surface_stroke,
    _cairo_tg_image_surface_fill,
    _cairo_tg_image_surface_glyphs,
};

typedef struct _cairo_tg_surface_tile
{
    cairo_surface_t	    *surface;
    cairo_rectangle_int_t   tile_rect;
} cairo_tg_surface_tile_t;

static inline int
_cairo_tg_surface_tiles_init (cairo_tg_surface_t	    *surface,
			      const cairo_rectangle_int_t   *extents,
			      int			    num_tiles,
			      cairo_tg_surface_tile_t	    *tiles)
{
    int tile_height;
    int i;

    if (extents->height <= 0)
	return 0;

    if (extents->height <= num_tiles)
	num_tiles = extents->height;

    tile_height = extents->height / num_tiles;

    for (i = 0; i < num_tiles; i++)
    {
	tiles[i].surface = surface->tile_surfaces[i];
	tiles[i].tile_rect.x = extents->x;
	tiles[i].tile_rect.y = extents->y + i * tile_height;
	tiles[i].tile_rect.width = extents->width;
	tiles[i].tile_rect.height = tile_height;
    }

    tiles[num_tiles - 1].tile_rect.height = extents->height - i * (num_tiles - 1);

    return num_tiles;
}

static cairo_int_status_t
_cairo_tg_surface_tile_paint (void		    *closure,
			      cairo_operator_t	    op,
			      const cairo_pattern_t *source,
			      const cairo_clip_t    *clip)
{
    cairo_tg_surface_tile_t *tile = (cairo_tg_surface_tile_t *) closure;
    cairo_clip_t	    *tile_clip;
    cairo_int_status_t	    status = CAIRO_INT_STATUS_SUCCESS;

    tile_clip = _cairo_clip_copy_intersect_rectangle (clip, &tile->tile_rect);

    if (! _cairo_clip_is_all_clipped (tile_clip))
	status = _cairo_tg_image_surface_paint (tile->surface, op, source, tile_clip);

    _cairo_clip_destroy (tile_clip);

    return status;
}

static cairo_int_status_t
_cairo_tg_surface_tile_mask (void		    *closure,
			     cairo_operator_t	    op,
			     const cairo_pattern_t  *source,
			     const cairo_pattern_t  *mask,
			     const cairo_clip_t	    *clip)
{
    cairo_tg_surface_tile_t *tile = (cairo_tg_surface_tile_t *) closure;
    cairo_clip_t	    *tile_clip;
    cairo_int_status_t	    status = CAIRO_INT_STATUS_SUCCESS;

    tile_clip = _cairo_clip_copy_intersect_rectangle (clip, &tile->tile_rect);

    if (! _cairo_clip_is_all_clipped (tile_clip))
    {
	status = _cairo_tg_image_surface_mask (tile->surface, op, source,
					       mask, tile_clip);
    }

    _cairo_clip_destroy (tile_clip);

    return status;
}

static cairo_int_status_t
_cairo_tg_surface_tile_stroke (void			    *closure,
			       cairo_operator_t		    op,
			       const cairo_pattern_t	    *source,
			       const cairo_path_fixed_t	    *path,
			       const cairo_stroke_style_t   *style,
			       const cairo_matrix_t	    *ctm,
			       const cairo_matrix_t	    *ctm_inverse,
			       double			    tolerance,
			       cairo_antialias_t	    antialias,
			       const cairo_clip_t	    *clip)
{
    cairo_tg_surface_tile_t *tile = (cairo_tg_surface_tile_t *) closure;
    cairo_clip_t	    *tile_clip;
    cairo_int_status_t	    status = CAIRO_INT_STATUS_SUCCESS;

    tile_clip = _cairo_clip_copy_intersect_rectangle (clip, &tile->tile_rect);

    if (! _cairo_clip_is_all_clipped (tile_clip))
    {
	status = _cairo_tg_image_surface_stroke (tile->surface, op, source,
						 path, style, ctm, ctm_inverse,
						 tolerance, antialias, tile_clip);
    }

    _cairo_clip_destroy (tile_clip);

    return status;
}

static cairo_int_status_t
_cairo_tg_surface_tile_fill (void			*closure,
			     cairo_operator_t		op,
			     const cairo_pattern_t	*source,
			     const cairo_path_fixed_t	*path,
			     cairo_fill_rule_t		fill_rule,
			     double			tolerance,
			     cairo_antialias_t		antialias,
			     const cairo_clip_t		*clip)
{
    cairo_tg_surface_tile_t *tile = (cairo_tg_surface_tile_t *) closure;
    cairo_clip_t	    *tile_clip;
    cairo_int_status_t	    status = CAIRO_INT_STATUS_SUCCESS;

    tile_clip = _cairo_clip_copy_intersect_rectangle (clip, &tile->tile_rect);

    if (! _cairo_clip_is_all_clipped (tile_clip))
    {
	status = _cairo_tg_image_surface_fill (tile->surface, op, source,
					       path, fill_rule, tolerance,
					       antialias, tile_clip);
    }

    _cairo_clip_destroy (tile_clip);

    return status;
}

static cairo_int_status_t
_cairo_tg_surface_tile_glyphs (void			*closure,
			       cairo_operator_t		op,
			       const cairo_pattern_t	*source,
			       cairo_glyph_t		*glyphs,
			       int			num_glyphs,
			       cairo_scaled_font_t	*scaled_font,
			       const cairo_clip_t	*clip)
{
    cairo_tg_surface_tile_t *tile = (cairo_tg_surface_tile_t *) closure;
    cairo_clip_t	    *tile_clip;
    cairo_int_status_t	    status = CAIRO_INT_STATUS_SUCCESS;

    tile_clip = _cairo_clip_copy_intersect_rectangle (clip, &tile->tile_rect);

    if (! _cairo_clip_is_all_clipped (tile_clip))
    {
	status = _cairo_tg_image_surface_glyphs (tile->surface, op, source,
						 glyphs, num_glyphs, scaled_font,
						 tile_clip);
    }

    _cairo_clip_destroy (tile_clip);

    return status;
}

const cairo_tg_journal_replay_funcs_t replay_funcs_tile =
{
    _cairo_tg_surface_tile_paint,
    _cairo_tg_surface_tile_mask,
    _cairo_tg_surface_tile_stroke,
    _cairo_tg_surface_tile_fill,
    _cairo_tg_surface_tile_glyphs,
};

#if ! CAIRO_HAS_OPENMP
#define CAIRO_TG_NUM_MAX_WORKERS    CAIRO_TG_NUM_MAX_TILES

typedef enum _cairo_tg_worker_status
{
    CAIRO_TG_WORKER_STATUS_IDLE,	/* can transit to either OCCUPIED or KILLED */
    CAIRO_TG_WORKER_STATUS_TO_DO,	/* only can transit to IDLE state */
    CAIRO_TG_WORKER_STATUS_KILLED,	/* worker will be no longer valid */
} cairo_tg_worker_status_t;

typedef struct _cairo_tg_worker
{
    cairo_tg_journal_t		*journal;
    cairo_tg_surface_tile_t	*tile;

    pthread_t			thread;
    pthread_mutex_t		lock;
    pthread_cond_t		cond_wake_up;
    cairo_tg_worker_status_t	status;

#ifdef CAIRO_TG_THREAD_POOL_BUSY_WAIT
    pthread_spinlock_t		spinlock;
#else
    pthread_cond_t		cond_done;
#endif
} cairo_tg_worker_t;

cairo_tg_worker_t   workers[CAIRO_TG_NUM_MAX_WORKERS];

pthread_mutex_t	workers_lock;
cairo_bool_t	workers_occupied;

static void *
_cairo_tg_worker_mainloop (void *arg)
{
    cairo_tg_worker_t	*worker = (cairo_tg_worker_t *) arg;

    while (1)
    {
	pthread_mutex_lock (&worker->lock);

	while (worker->status == CAIRO_TG_WORKER_STATUS_IDLE)
	    pthread_cond_wait (&worker->cond_wake_up, &worker->lock);

	/* Here, worker is kicked off to do some action. */

	if (worker->status == CAIRO_TG_WORKER_STATUS_KILLED)
	{
	    /* Worker is killed, so release mutex and exit. */
	    pthread_mutex_unlock (&worker->lock);
	    pthread_exit (NULL);
	}

	assert (worker->status == CAIRO_TG_WORKER_STATUS_TO_DO);

	_cairo_tg_journal_replay (worker->journal, (void *)worker->tile,
				  &worker->tile->tile_rect, &replay_funcs_tile);

	worker->status = CAIRO_TG_WORKER_STATUS_IDLE;

#ifndef CAIRO_TG_THREAD_POOL_BUSY_WAIT
	pthread_cond_signal (&worker->cond_done);
#endif

	pthread_mutex_unlock (&worker->lock);
    }

    return NULL;
}

static void
_cairo_tg_workers_init (void)
{
    int i;

    for (i = 0; i < CAIRO_TG_NUM_MAX_WORKERS; i++)
    {
	workers[i].status = CAIRO_TG_WORKER_STATUS_IDLE;

	pthread_mutex_init (&workers[i].lock, NULL);
	pthread_cond_init (&workers[i].cond_wake_up, NULL);

#ifdef CAIRO_TG_THREAD_POOL_BUSY_WAIT
	pthread_spin_init (&workers[i].spinlock, 0);
#else
	pthread_cond_init (&workers[i].cond_done, NULL);
#endif

	pthread_create (&workers[i].thread, NULL, _cairo_tg_worker_mainloop, (void *) &workers[i]);
    }

    pthread_mutex_init (&workers_lock, NULL);
    workers_occupied = FALSE;
}

static void
_cairo_tg_workers_fini (void)
{
    int i;

    for (i = 0; i < CAIRO_TG_NUM_MAX_WORKERS; i++)
    {
	pthread_mutex_lock (&workers[i].lock);

	workers[i].status = CAIRO_TG_WORKER_STATUS_KILLED;
	pthread_cond_signal (&workers[i].cond_wake_up);
	pthread_mutex_unlock (&workers[i].lock);
    }

    for (i = 0; i < CAIRO_TG_NUM_MAX_WORKERS; i++)
	pthread_join (workers[i].thread, NULL);

    for (i = 0; i < CAIRO_TG_NUM_MAX_WORKERS; i++)
    {
	pthread_mutex_destroy (&workers[i].lock);
	pthread_cond_destroy (&workers[i].cond_wake_up);

#ifdef CAIRO_TG_THREAD_POOL_BUSY_WAIT
	pthread_spin_destroy (&workers[i].spinlock);
#else
	pthread_cond_destroy (&workers[i].cond_done);
#endif
    }
}

static void __attribute__((constructor))
_cairo_tg_constructor (void)
{
    pthread_atfork (NULL, NULL, _cairo_tg_workers_init);
    _cairo_tg_workers_init ();
}

static void __attribute__((destructor))
_cairo_tg_destructor (void)
{
    _cairo_tg_workers_fini ();
}

#endif /* ! CAIRO_HAS_OPENMP */

static void
_cairo_tg_surface_prepare_flush_parallel (cairo_tg_surface_t *surface)
{
    const cairo_tg_journal_entry_t  *entry;
    const cairo_tg_journal_entry_t  *next;

    cairo_list_foreach_entry_safe (entry, next, cairo_tg_journal_entry_t,
				   &surface->journal.entry_list, link)
    {
	if (entry->source.base.type == CAIRO_PATTERN_TYPE_SURFACE)
	{
	    cairo_surface_pattern_t *pattern = (cairo_surface_pattern_t *) (&entry->source.base);
	    cairo_surface_flush (pattern->surface);
	}

	if (entry->type == CAIRO_TG_JOURNAL_ENTRY_MASK)
	{
	    cairo_tg_journal_entry_mask_t *e =
		(cairo_tg_journal_entry_mask_t *) entry;

	    if (e->mask.base.type == CAIRO_PATTERN_TYPE_SURFACE)
	    {
		cairo_surface_pattern_t *pattern = (cairo_surface_pattern_t *) (&e->mask.base);
		cairo_surface_flush (pattern->surface);
	    }
	}
    }
}

static cairo_int_status_t
_cairo_tg_surface_flush_parallel (cairo_tg_surface_t *surface)
{
    int				num_tiles, i;
    cairo_tg_surface_tile_t	tiles[CAIRO_TG_NUM_MAX_TILES];
    cairo_rectangle_int_t	extents;

    if (surface->journal.num_entries < CAIRO_TG_NUM_MIN_ENTRIES_FOR_PARALLEL_FLUSH)
	return CAIRO_INT_STATUS_UNSUPPORTED;

    _cairo_tg_surface_prepare_flush_parallel (surface);

    extents.x = 0;
    extents.y = 0;
    extents.width = surface->width;
    extents.height = surface->height;

    _cairo_rectangle_intersect (&extents, &surface->journal.extents);

    num_tiles = get_num_cpu_cores ();

#if ! CAIRO_HAS_OPENMP
    if (num_tiles > CAIRO_TG_NUM_MAX_WORKERS)
	num_tiles = CAIRO_TG_NUM_MAX_WORKERS;
#endif

    num_tiles = _cairo_tg_surface_tiles_init (surface, &extents, num_tiles, &tiles[0]);

#if CAIRO_HAS_OPENMP
    #pragma omp parallel for
    for (i = 0; i < num_tiles; i++)
    {
	_cairo_tg_journal_replay (&surface->journal, (void *) &tiles[i],
				  &tiles[i].tile_rect, &replay_funcs_tile);
    }
#else
    pthread_mutex_lock (&workers_lock);

    if (workers_occupied)
    {
	pthread_mutex_unlock (&workers_lock);
	return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    workers_occupied = TRUE;
    pthread_mutex_unlock (&workers_lock);

    /* Kick workers to start. */
    for (i = 0; i < num_tiles - 1; i++)
    {
	pthread_mutex_lock (&workers[i].lock);

	workers[i].status = CAIRO_TG_WORKER_STATUS_TO_DO;
	workers[i].journal = &surface->journal;
	workers[i].tile = &tiles[i];

	pthread_cond_signal (&workers[i].cond_wake_up);
	pthread_mutex_unlock (&workers[i].lock);
    }

    _cairo_tg_journal_replay (&surface->journal, &tiles[num_tiles - 1],
			      &tiles[num_tiles - 1].tile_rect, &replay_funcs_tile);

    /* Wait for workers to finish. */
    for (i = 0; i < num_tiles - 1; i++)
    {
#ifdef CAIRO_TG_THREAD_POOL_BUSY_WAIT
	pthread_spin_lock (&workers[i].spinlock);

	while (workers[i].status == CAIRO_TG_WORKER_STATUS_TO_DO)
	{
	    pthread_spin_unlock (&workers[i].spinlock);
	    pthread_spin_lock (&workers[i].spinlock);
	}

	pthread_spin_unlock (&workers[i].spinlock);
#else
	pthread_mutex_lock (&workers[i].lock);

	while (workers[i].status == CAIRO_TG_WORKER_STATUS_TO_DO)
	    pthread_cond_wait (&workers[i].cond_done, &workers[i].lock);

	pthread_mutex_unlock (&workers[i].lock);
#endif
    }

    /* Release thread pool. */
    pthread_mutex_lock (&workers_lock);
    workers_occupied = FALSE;
    pthread_mutex_unlock (&workers_lock);
#endif

    return CAIRO_INT_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_tg_surface_flush (void	    *abstract_surface,
			 unsigned   flags)
{
    cairo_tg_surface_t	*surface = abstract_surface;
    cairo_int_status_t	status = CAIRO_INT_STATUS_SUCCESS;

    if (flags)
	return CAIRO_STATUS_SUCCESS;

    _cairo_tg_journal_lock (&surface->journal);

    if (surface->journal.num_entries)
    {
	status = _cairo_tg_surface_flush_parallel (surface);

	if (status)
	{
	    status = _cairo_tg_journal_replay (&surface->journal,
					       (void *) surface->image_surface,
					       NULL, &replay_funcs_image_fallback);
	}

	_cairo_tg_journal_clear (&surface->journal);
    }

    _cairo_tg_journal_unlock (&surface->journal);

    return status;
}

static cairo_image_surface_t *
_cairo_tg_surface_map_to_image (void			    *abstract_surface,
				const cairo_rectangle_int_t *extents)
{
    cairo_tg_surface_t	*other = abstract_surface;
    cairo_surface_t	*surface;
    uint8_t		*buffer;

    _cairo_tg_surface_flush (other, 0);

    buffer = other->data;
    buffer += extents->y * other->stride;
    buffer += extents->x * other->bpp / 8;

    surface =
	_cairo_image_surface_create_with_pixman_format (buffer,
							other->pixman_format,
							extents->width,
							extents->height,
							other->stride);

    if (unlikely (surface == NULL))
	return NULL;

    cairo_surface_set_device_offset (surface, -extents->x, extents->y);

    return (cairo_image_surface_t *) surface;
}

static cairo_int_status_t
_cairo_tg_surface_unmap_image (void			*abstract_surface,
			       cairo_image_surface_t	*image)
{
    cairo_surface_finish (&image->base);
    cairo_surface_destroy (&image->base);

    return CAIRO_INT_STATUS_SUCCESS;
}

static cairo_bool_t
_cairo_tg_surface_get_extents (void			*abstract_surface,
			       cairo_rectangle_int_t	*extents)
{
    cairo_tg_surface_t *surface = abstract_surface;

    extents->x = 0;
    extents->y = 0;
    extents->width = surface->width;
    extents->height = surface->height;

    return TRUE;
}

static cairo_int_status_t
_cairo_tg_surface_paint (void			*abstract_surface,
			 cairo_operator_t	op,
			 const cairo_pattern_t	*source,
			 const cairo_clip_t	*clip)
{
    cairo_tg_surface_t	*surface = abstract_surface;
    cairo_int_status_t	status = CAIRO_INT_STATUS_UNSUPPORTED;

    if (! _cairo_pattern_is_self_copy (&surface->base, source) &&
	! _cairo_pattern_is_recording (source))
	status = _cairo_tg_journal_log_paint (&surface->journal, op, source, clip);

    if (status)
    {
	status = _cairo_tg_surface_flush (surface, 0);

	if (unlikely (status))
	    return status;

	status =  _cairo_tg_image_surface_paint (surface->image_surface, op, source, clip);
    }

    return status;
}

static cairo_int_status_t
_cairo_tg_surface_mask (void			*abstract_surface,
			cairo_operator_t	op,
			const cairo_pattern_t	*source,
			const cairo_pattern_t	*mask,
			const cairo_clip_t	*clip)
{
    cairo_tg_surface_t	*surface = abstract_surface;
    cairo_int_status_t	status = CAIRO_INT_STATUS_UNSUPPORTED;

    if (! _cairo_pattern_is_self_copy (&surface->base, source) &&
	! _cairo_pattern_is_self_copy (&surface->base, mask) &&
	! _cairo_pattern_is_recording (source))
	status = _cairo_tg_journal_log_mask (&surface->journal, op, source, mask, clip);

    if (status)
    {
	status = _cairo_tg_surface_flush (surface, 0);

	if (unlikely (status))
	    return status;

	status =  _cairo_tg_image_surface_mask (surface->image_surface, op, source,
						mask, clip);
    }

    return status;
}

static cairo_int_status_t
_cairo_tg_surface_stroke (void				*abstract_surface,
			  cairo_operator_t		op,
			  const cairo_pattern_t		*source,
			  const cairo_path_fixed_t	*path,
			  const cairo_stroke_style_t	*style,
			  const cairo_matrix_t		*ctm,
			  const cairo_matrix_t		*ctm_inverse,
			  double			tolerance,
			  cairo_antialias_t		antialias,
			  const cairo_clip_t		*clip)
{
    cairo_tg_surface_t	*surface = abstract_surface;
    cairo_int_status_t	status = CAIRO_INT_STATUS_UNSUPPORTED;

    if (! _cairo_pattern_is_self_copy (&surface->base, source) &&
	! _cairo_pattern_is_recording (source))
    {
	status = _cairo_tg_journal_log_stroke (&surface->journal, op, source,
					       path, style, ctm, ctm_inverse,
					       tolerance, antialias, clip);
    }

    if (status)
    {
	status = _cairo_tg_surface_flush (surface, 0);

	if (unlikely (status))
	    return status;

	status = _cairo_tg_image_surface_stroke (surface->image_surface, op, source,
						 path, style, ctm, ctm_inverse,
						 tolerance, antialias, clip);
    }

    return status;
}

static cairo_int_status_t
_cairo_tg_surface_fill (void			    *abstract_surface,
			cairo_operator_t	    op,
			const cairo_pattern_t	    *source,
			const cairo_path_fixed_t    *path,
			cairo_fill_rule_t	    fill_rule,
			double			    tolerance,
			cairo_antialias_t	    antialias,
			const cairo_clip_t	    *clip)
{
    cairo_tg_surface_t	*surface = abstract_surface;
    cairo_int_status_t	status = CAIRO_INT_STATUS_UNSUPPORTED;

    if (! _cairo_pattern_is_self_copy (&surface->base, source) &&
	! _cairo_pattern_is_recording (source))
    {
	status = _cairo_tg_journal_log_fill (&surface->journal, op, source,
					     path, fill_rule, tolerance, antialias, clip);
    }

    if (status)
    {
	status = _cairo_tg_surface_flush (surface, 0);

	if (unlikely (status))
	    return status;

	status =  _cairo_tg_image_surface_fill (surface->image_surface, op, source,
						path, fill_rule, tolerance, antialias, clip);
    }

    return status;
}

static cairo_int_status_t
_cairo_tg_surface_glyphs (void			*abstract_surface,
			  cairo_operator_t	op,
			  const cairo_pattern_t	*source,
			  cairo_glyph_t		*glyphs,
			  int			num_glyphs,
			  cairo_scaled_font_t	*scaled_font,
			  const cairo_clip_t	*clip)
{
    cairo_tg_surface_t	*surface = abstract_surface;
    cairo_int_status_t	status = CAIRO_INT_STATUS_UNSUPPORTED;

    if (! _cairo_pattern_is_self_copy (&surface->base, source) &&
	! _cairo_pattern_is_recording (source))
    {
	status = _cairo_tg_journal_log_glyphs (&surface->journal, op, source,
					       glyphs, num_glyphs, scaled_font, clip);
    }

    if (status)
    {
	status = _cairo_tg_surface_flush (surface, 0);

	if (unlikely (status))
	    return status;

	status = _cairo_tg_image_surface_glyphs (surface->image_surface, op, source,
						 glyphs, num_glyphs, scaled_font, clip);
    }

    return status;
}

static cairo_surface_t *
_cairo_tg_surface_create_similar (void		    *abstract_other,
				  cairo_content_t   content,
				  int		    width,
				  int		    height)
{
    cairo_tg_surface_t *other = abstract_other;

    if (! _cairo_tg_surface_is_size_valid (width, height))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_SIZE));

    if (content == other->base.content)
	return cairo_tg_surface_create (other->format, width, height);

    return cairo_tg_surface_create (_cairo_format_from_content (content), width, height);
}

static cairo_surface_t *
_cairo_tg_surface_source (void			*abstract_surface,
			  cairo_rectangle_int_t	*extents)
{
    cairo_tg_surface_t *surface = abstract_surface;

    if (extents)
    {
	extents->x = extents->y = 0;
	extents->width = surface->width;
	extents->height = surface->height;
    }

    return &surface->base;
}

static cairo_status_t
_cairo_tg_surface_acquire_source_image (void			*abstract_surface,
					cairo_image_surface_t	**image_out,
					void			**image_extra)
{
    cairo_tg_surface_t *surface = abstract_surface;

    _cairo_tg_surface_flush (surface, 0);

    *image_out = (cairo_image_surface_t *) surface->image_surface;
    *image_extra = NULL;

    return CAIRO_STATUS_SUCCESS;
}

static void
_cairo_tg_surface_release_source_image (void			*abstract_surface,
					cairo_image_surface_t	*image,
					void			*image_extra)
{
    /* Do nothing */
}

static cairo_surface_t *
_cairo_tg_surface_snapshot (void *abstract_surface)
{
    cairo_tg_surface_t *surface = abstract_surface;
    cairo_tg_surface_t *clone;

    _cairo_tg_surface_flush (surface, 0);

    if (_cairo_tg_surface_owns_data (surface) && surface->base._finishing)
    {
	return cairo_tg_surface_create_for_data (surface->data, surface->format,
						 surface->width, surface->height,
						 surface->stride);
    }

    clone = (cairo_tg_surface_t *)
	cairo_tg_surface_create (surface->format, surface->width, surface->height);

    if (unlikely (clone->base.status))
	return &clone->base;

    if (surface->stride == clone->stride)
    {
	memcpy (clone->data, surface->data, clone->stride * clone->height);
    }
    else
    {
	unsigned char *dst = clone->data;
	unsigned char *src = surface->data;
	int i;
	int stride = clone->stride < surface->stride ? clone->stride : surface->stride;

	for (i = 0; i < clone->height; i++)
	{
	    memcpy (dst, src, stride);
	    dst += clone->stride;
	    src += surface->stride;
	}
    }

    clone->base.is_clear = FALSE;

    return &clone->base;
}

static cairo_int_status_t
_cairo_tg_surface_init_tile_surfaces (cairo_tg_surface_t *surface)
{
    int			i;
    cairo_int_status_t	status = CAIRO_INT_STATUS_SUCCESS;

    memset (&surface->tile_surfaces[0], 0x00,
	    sizeof (cairo_surface_t *) * CAIRO_TG_NUM_MAX_TILES);

    for (i = 0; i < CAIRO_TG_NUM_MAX_TILES; i++)
    {
	surface->tile_surfaces[i] = cairo_image_surface_create_for_data (surface->data,
									 surface->format,
									 surface->width,
									 surface->height,
									 surface->stride);

	if (surface->tile_surfaces[i] == NULL)
	{
	    status = CAIRO_INT_STATUS_NO_MEMORY;
	    break;
	}
    }

    if (unlikely (status))
    {
	for (i = 0; i < CAIRO_TG_NUM_MAX_TILES; i++)
	{
	    if (surface->tile_surfaces[i])
		cairo_surface_destroy (surface->tile_surfaces[i]);
	    else
		break;
	}
    }

    return status;
}

static void
_cairo_tg_surface_fini_tile_surfaces (cairo_tg_surface_t *surface)
{
    int	i;

    for (i = 0; i < CAIRO_TG_NUM_MAX_TILES; i++)
    {
	if (surface->tile_surfaces[i])
	    cairo_surface_destroy (surface->tile_surfaces[i]);
	else
	    break;
    }
}

static cairo_status_t
_cairo_tg_surface_finish (void *abstract_surface)
{
    cairo_tg_surface_t *surface = abstract_surface;

    _cairo_tg_surface_flush (surface, 0);
    _cairo_tg_journal_fini (&surface->journal);
    _cairo_tg_surface_fini_tile_surfaces (surface);
    cairo_surface_destroy (surface->image_surface);

    return CAIRO_STATUS_SUCCESS;
}

static const cairo_surface_backend_t _cairo_tg_surface_backend =
{
    CAIRO_SURFACE_TYPE_TG,
    _cairo_tg_surface_finish,

    _cairo_default_context_create,

    _cairo_tg_surface_create_similar,
    NULL, /* create_similar image */
    _cairo_tg_surface_map_to_image,
    _cairo_tg_surface_unmap_image,

    _cairo_tg_surface_source,
    _cairo_tg_surface_acquire_source_image,
    _cairo_tg_surface_release_source_image,
    _cairo_tg_surface_snapshot,

    NULL, /* copy_page */
    NULL, /* show_page */

    _cairo_tg_surface_get_extents,
    NULL, /* get_font_options */

    _cairo_tg_surface_flush,
    NULL, /* mark_dirty_rectangle */

    _cairo_tg_surface_paint,
    _cairo_tg_surface_mask,
    _cairo_tg_surface_stroke,
    _cairo_tg_surface_fill,
    NULL, /* fill_stroke */
    _cairo_tg_surface_glyphs,
};

cairo_surface_t *
cairo_tg_surface_create (cairo_format_t	format,
			 int		width,
			 int		height)
{
    cairo_tg_surface_t	*surface;
    cairo_surface_t	*image_surface;

    image_surface = cairo_image_surface_create (format, width, height);

    if (unlikely (image_surface == NULL))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    surface = malloc (sizeof (cairo_tg_surface_t));

    if (unlikely (surface == NULL))
    {
	cairo_surface_destroy (image_surface);

	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));
    }

    _cairo_surface_init (&surface->base,
			 &_cairo_tg_surface_backend,
			 NULL, image_surface->content);

    surface->format = format;
    surface->pixman_format = ((cairo_image_surface_t *) image_surface)->pixman_format;
    surface->data = (unsigned char *) cairo_image_surface_get_data (image_surface);
    surface->width = width;
    surface->height = height;
    surface->stride = cairo_image_surface_get_stride (image_surface);
    surface->bpp = get_bpp_for_format (format);
    surface->image_surface = image_surface;
    surface->base.is_clear = image_surface->is_clear;

    _cairo_tg_journal_init (&surface->journal);

    if (_cairo_tg_surface_init_tile_surfaces (surface))
    {
	cairo_surface_destroy (image_surface);
	_cairo_tg_journal_fini (&surface->journal);

	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));
    }

    return &surface->base;
}

cairo_surface_t *
cairo_tg_surface_create_for_data (unsigned char	    *data,
				  cairo_format_t    format,
				  int		    width,
				  int		    height,
				  int		    stride)
{
    cairo_tg_surface_t	*surface;
    cairo_surface_t	*image_surface;

    image_surface = cairo_image_surface_create_for_data (data, format, width, height, stride);

    if (unlikely (image_surface == NULL))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    surface = malloc (sizeof (cairo_tg_surface_t));

    if (unlikely (surface == NULL))
    {
	cairo_surface_destroy (image_surface);

	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));
    }

    _cairo_surface_init (&surface->base,
			 &_cairo_tg_surface_backend,
			 NULL, image_surface->content);

    surface->format = format;
    surface->pixman_format = ((cairo_image_surface_t *) image_surface)->pixman_format;
    surface->data = (unsigned char *) cairo_image_surface_get_data (image_surface);
    surface->width = width;
    surface->height = height;
    surface->stride = cairo_image_surface_get_stride (image_surface);
    surface->bpp = get_bpp_for_format (format);
    surface->image_surface = image_surface;
    surface->base.is_clear = image_surface->is_clear;

    _cairo_tg_journal_init (&surface->journal);

    if (_cairo_tg_surface_init_tile_surfaces (surface))
    {
	cairo_surface_destroy (image_surface);
	_cairo_tg_journal_fini (&surface->journal);

	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));
    }

    return &surface->base;
}

unsigned char *
cairo_tg_surface_get_data (cairo_surface_t *surface)
{
    cairo_tg_surface_t *tg_surface = (cairo_tg_surface_t *) surface;

    if (! _cairo_surface_is_tg (surface)) {
	_cairo_error_throw (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);
	return NULL;
    }

    return tg_surface->data;
}

cairo_format_t
cairo_tg_surface_get_format (cairo_surface_t *surface)
{
    cairo_tg_surface_t *tg_surface = (cairo_tg_surface_t *) surface;

    if (! _cairo_surface_is_tg (surface)) {
	_cairo_error_throw (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);
	return CAIRO_FORMAT_INVALID;
    }

    return tg_surface->format;
}

int
cairo_tg_surface_get_width (cairo_surface_t *surface)
{
    cairo_tg_surface_t *tg_surface = (cairo_tg_surface_t *) surface;

    if (! _cairo_surface_is_tg (surface)) {
	_cairo_error_throw (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);
	return 0;
    }

    return tg_surface->width;
}

int
cairo_tg_surface_get_height (cairo_surface_t *surface)
{
    cairo_tg_surface_t *tg_surface = (cairo_tg_surface_t *) surface;

    if (! _cairo_surface_is_tg (surface)) {
	_cairo_error_throw (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);
	return 0;
    }

    return tg_surface->height;
}

int
cairo_tg_surface_get_stride (cairo_surface_t *surface)
{
    cairo_tg_surface_t *tg_surface = (cairo_tg_surface_t *) surface;

    if (! _cairo_surface_is_tg (surface)) {
	_cairo_error_throw (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);
	return 0;
    }

    return tg_surface->stride;
}
