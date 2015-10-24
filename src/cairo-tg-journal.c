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

#include "cairo-tg.h"
#include "cairo-tg-private.h"
#include "cairo-tg-journal-private.h"
#include "cairo-tg-allocator-private.h"
#include "cairo-tg-composite-extents-private.h"

static inline cairo_int_status_t
_cairo_tg_journal_pattern_snapshot (cairo_pattern_t	    *dst,
				    const cairo_pattern_t   *src)
{
    return _cairo_pattern_init_snapshot (dst, src);
}

/* Allocator for various types of journal entries. */
static cairo_tg_journal_entry_t *
_cairo_tg_journal_entry_alloc (cairo_tg_mono_allocator_t *allocator,
			       cairo_tg_journal_entry_type_t type)
{
    cairo_tg_journal_entry_t *entry = NULL;

    switch (type)
    {
    case CAIRO_TG_JOURNAL_ENTRY_PAINT:
	entry = _cairo_tg_mono_allocator_alloc (allocator,
						sizeof (cairo_tg_journal_entry_paint_t));
	break;
    case CAIRO_TG_JOURNAL_ENTRY_MASK:
	entry = _cairo_tg_mono_allocator_alloc (allocator,
						sizeof (cairo_tg_journal_entry_mask_t));
	break;
    case CAIRO_TG_JOURNAL_ENTRY_STROKE:
	entry = _cairo_tg_mono_allocator_alloc (allocator,
						sizeof (cairo_tg_journal_entry_stroke_t));
	break;
    case CAIRO_TG_JOURNAL_ENTRY_FILL:
	entry = _cairo_tg_mono_allocator_alloc (allocator,
						sizeof (cairo_tg_journal_entry_fill_t));
	break;
    case CAIRO_TG_JOURNAL_ENTRY_GLYPHS:
	entry = _cairo_tg_mono_allocator_alloc (allocator,
						sizeof (cairo_tg_journal_entry_glyphs_t));
	break;
    default:
	ASSERT_NOT_REACHED;
	return NULL;
    }

    /* One should not change the type of an entry.
     * It is determined at the moment of allocation. */
    entry->type = type;

    return entry;
}

static void
_cairo_tg_journal_entry_fini (cairo_tg_journal_entry_t *entry)
{
    /* common part. */
    _cairo_pattern_fini (&entry->source.base);

    if (entry->clip)
	_cairo_clip_destroy (entry->clip);

    /* For each entry types... */
    switch (entry->type)
    {
    case CAIRO_TG_JOURNAL_ENTRY_PAINT:
	break;
    case CAIRO_TG_JOURNAL_ENTRY_MASK:
	{
	    cairo_tg_journal_entry_mask_t *entry_mask =
		(cairo_tg_journal_entry_mask_t *) entry;

	    _cairo_pattern_fini (&entry_mask->mask.base);
	}
	break;
    case CAIRO_TG_JOURNAL_ENTRY_STROKE:
	{
	    cairo_tg_journal_entry_stroke_t *entry_stroke =
		(cairo_tg_journal_entry_stroke_t *) entry;

	    _cairo_path_fixed_fini (&entry_stroke->path);
	    _cairo_stroke_style_fini (&entry_stroke->style);
	}
	break;
    case CAIRO_TG_JOURNAL_ENTRY_FILL:
	{
	    cairo_tg_journal_entry_fill_t *entry_fill =
		(cairo_tg_journal_entry_fill_t *) entry;

	    _cairo_path_fixed_fini (&entry_fill->path);
	}
	break;
    case CAIRO_TG_JOURNAL_ENTRY_GLYPHS:
	{
	    cairo_tg_journal_entry_glyphs_t *entry_glyphs =
		(cairo_tg_journal_entry_glyphs_t *) entry;

	    free (entry_glyphs->glyphs);
	    cairo_scaled_font_destroy (entry_glyphs->scaled_font);
	}
	break;
    default:
	ASSERT_NOT_REACHED;
    }
}

cairo_int_status_t
_cairo_tg_journal_init (cairo_tg_journal_t *journal)
{
    cairo_int_status_t status;

    CAIRO_MUTEX_INIT (journal->mutex);
    journal->num_entries = 0;
    cairo_list_init (&journal->entry_list);
    journal->extents = _cairo_empty_rectangle;

    status =  _cairo_tg_mono_allocator_init (&journal->allocator, 4096 -
					     sizeof (cairo_tg_mem_chunk_t));

    return status;
}

void
_cairo_tg_journal_fini (cairo_tg_journal_t *journal)
{
    cairo_tg_journal_entry_t *entry;
    cairo_tg_journal_entry_t *next;

    CAIRO_MUTEX_FINI (journal->mutex);
    cairo_list_foreach_entry_safe (entry, next, cairo_tg_journal_entry_t,
				   &journal->entry_list, link)
    {
	_cairo_tg_journal_entry_fini (entry);
    }

    _cairo_tg_mono_allocator_fini (&journal->allocator);
}

void
_cairo_tg_journal_lock (cairo_tg_journal_t *journal)
{
    CAIRO_MUTEX_LOCK (journal->mutex);
}

void
_cairo_tg_journal_unlock (cairo_tg_journal_t *journal)
{
    CAIRO_MUTEX_UNLOCK (journal->mutex);
}

cairo_int_status_t
_cairo_tg_journal_log_paint (cairo_tg_journal_t	    *journal,
			     cairo_operator_t	    op,
			     const cairo_pattern_t  *source,
			     const cairo_clip_t	    *clip)
{
    cairo_int_status_t		    status;
    cairo_tg_journal_entry_paint_t  *entry;

    entry = (cairo_tg_journal_entry_paint_t *)
	_cairo_tg_journal_entry_alloc (&journal->allocator, CAIRO_TG_JOURNAL_ENTRY_PAINT);

    if (unlikely (entry == NULL))
	return CAIRO_INT_STATUS_NO_MEMORY;

    status = _cairo_tg_journal_pattern_snapshot (&entry->base.source.base, source);

    if (unlikely (status))
	return status;

    entry->base.op = op;
    entry->base.clip = _cairo_clip_copy (clip);
    _cairo_tg_approximate_paint_extents (&entry->base.extents, op, source, clip);
    _cairo_rectangle_union (&journal->extents, &entry->base.extents);

    cairo_list_add_tail (&entry->base.link, &journal->entry_list);
    journal->num_entries++;

    return CAIRO_INT_STATUS_SUCCESS;
}

cairo_int_status_t
_cairo_tg_journal_log_mask (cairo_tg_journal_t	    *journal,
			    cairo_operator_t	    op,
			    const cairo_pattern_t   *source,
			    const cairo_pattern_t   *mask,
			    const cairo_clip_t	    *clip)
{
    cairo_int_status_t		    status;
    cairo_tg_journal_entry_mask_t   *entry;

    entry = (cairo_tg_journal_entry_mask_t *)
	_cairo_tg_journal_entry_alloc (&journal->allocator, CAIRO_TG_JOURNAL_ENTRY_MASK);

    if (unlikely (entry == NULL))
	return CAIRO_INT_STATUS_NO_MEMORY;

    status = _cairo_tg_journal_pattern_snapshot (&entry->base.source.base, source);

    if (unlikely (status))
	return status;

    status = _cairo_tg_journal_pattern_snapshot (&entry->mask.base, mask);

    if (unlikely (status))
    {
	_cairo_pattern_fini (&entry->base.source.base);
	return status;
    }

    entry->base.op = op;
    entry->base.clip = _cairo_clip_copy (clip);
    _cairo_tg_approximate_mask_extents (&entry->base.extents, op, source, mask, clip);
    _cairo_rectangle_union (&journal->extents, &entry->base.extents);

    cairo_list_add_tail (&entry->base.link, &journal->entry_list);
    journal->num_entries++;

    return CAIRO_INT_STATUS_SUCCESS;
}

cairo_int_status_t
_cairo_tg_journal_log_stroke (cairo_tg_journal_t	    *journal,
			      cairo_operator_t		    op,
			      const cairo_pattern_t	    *source,
			      const cairo_path_fixed_t	    *path,
			      const cairo_stroke_style_t    *style,
			      const cairo_matrix_t	    *ctm,
			      const cairo_matrix_t	    *ctm_inverse,
			      double			    tolerance,
			      cairo_antialias_t		    antialias,
			      const cairo_clip_t	    *clip)
{
    cairo_int_status_t		    status;
    cairo_tg_journal_entry_stroke_t *entry;

    entry = (cairo_tg_journal_entry_stroke_t *)
	_cairo_tg_journal_entry_alloc (&journal->allocator, CAIRO_TG_JOURNAL_ENTRY_STROKE);

    if (unlikely (entry == NULL))
	return CAIRO_INT_STATUS_NO_MEMORY;

    status = _cairo_tg_journal_pattern_snapshot (&entry->base.source.base, source);

    if (unlikely (status))
	return status;

    status = _cairo_path_fixed_init_copy (&entry->path, path);

    if (unlikely (status))
    {
	_cairo_pattern_fini (&entry->base.source.base);
	return status;
    }

    status = _cairo_stroke_style_init_copy (&entry->style, style);

    if (unlikely (status))
    {
	_cairo_path_fixed_fini (&entry->path);
	_cairo_pattern_fini (&entry->base.source.base);
	return status;
    }

    entry->base.op = op;
    entry->base.clip = _cairo_clip_copy (clip);
    entry->ctm = *ctm;
    entry->ctm_inverse = *ctm_inverse;
    entry->tolerance = tolerance;
    entry->antialias = antialias;
    _cairo_tg_approximate_stroke_extents (&entry->base.extents, op, source,
					  path, style, ctm, ctm_inverse,
					  tolerance, antialias, clip);
    _cairo_rectangle_union (&journal->extents, &entry->base.extents);

    cairo_list_add_tail (&entry->base.link, &journal->entry_list);
    journal->num_entries++;

    return CAIRO_INT_STATUS_SUCCESS;
}

cairo_int_status_t
_cairo_tg_journal_log_fill (cairo_tg_journal_t		*journal,
			    cairo_operator_t		op,
			    const cairo_pattern_t	*source,
			    const cairo_path_fixed_t	*path,
			    cairo_fill_rule_t		fill_rule,
			    double			tolerance,
			    cairo_antialias_t		antialias,
			    const cairo_clip_t		*clip)
{
    cairo_int_status_t		    status;
    cairo_tg_journal_entry_fill_t   *entry;

    entry = (cairo_tg_journal_entry_fill_t *)
	_cairo_tg_journal_entry_alloc (&journal->allocator, CAIRO_TG_JOURNAL_ENTRY_FILL);

    if (unlikely (entry == NULL))
	return CAIRO_INT_STATUS_NO_MEMORY;

    status = _cairo_tg_journal_pattern_snapshot (&entry->base.source.base, source);

    if (unlikely (status))
	return status;

    status = _cairo_path_fixed_init_copy (&entry->path, path);

    if (unlikely (status))
    {
	_cairo_pattern_fini (&entry->base.source.base);
	return status;
    }

    entry->base.op = op;
    entry->base.clip = _cairo_clip_copy (clip);
    entry->fill_rule = fill_rule;
    entry->tolerance = tolerance;
    entry->antialias = antialias;
    _cairo_tg_approximate_fill_extents (&entry->base.extents, op, source,
					path, fill_rule, tolerance, antialias, clip);
    _cairo_rectangle_union (&journal->extents, &entry->base.extents);

    cairo_list_add_tail (&entry->base.link, &journal->entry_list);
    journal->num_entries++;

    return CAIRO_INT_STATUS_SUCCESS;
}

cairo_int_status_t
_cairo_tg_journal_log_glyphs (cairo_tg_journal_t    *journal,
			      cairo_operator_t	    op,
			      const cairo_pattern_t *source,
			      cairo_glyph_t	    *glyphs,
			      int		    num_glyphs,
			      cairo_scaled_font_t   *scaled_font,
			      const cairo_clip_t    *clip)
{
    cairo_int_status_t		    status;
    cairo_tg_journal_entry_glyphs_t *entry;

    entry = (cairo_tg_journal_entry_glyphs_t *)
	_cairo_tg_journal_entry_alloc (&journal->allocator, CAIRO_TG_JOURNAL_ENTRY_GLYPHS);

    if (unlikely (entry == NULL))
	return CAIRO_INT_STATUS_NO_MEMORY;

    status = _cairo_tg_journal_pattern_snapshot (&entry->base.source.base, source);

    if (unlikely (status))
	return status;

    entry->scaled_font = cairo_scaled_font_reference (scaled_font);

    if (unlikely (entry->scaled_font == NULL))
    {
	_cairo_pattern_fini (&entry->base.source.base);
	return CAIRO_INT_STATUS_NO_MEMORY;
    }

    if (num_glyphs)
    {
	entry->glyphs = malloc (sizeof (cairo_glyph_t) * num_glyphs);

	if (unlikely (entry->glyphs == NULL))
	{
	    cairo_scaled_font_destroy (entry->scaled_font);
	    _cairo_pattern_fini (&entry->base.source.base);
	    return CAIRO_INT_STATUS_NO_MEMORY;
	}

	memcpy (entry->glyphs, glyphs, sizeof (cairo_glyph_t) * num_glyphs);
    }
    else
    {
	entry->glyphs = NULL;
    }

    entry->num_glyphs = num_glyphs;
    entry->base.op = op;
    entry->base.clip = _cairo_clip_copy (clip);
    _cairo_tg_approximate_glyphs_extents (&entry->base.extents, op, source,
					  glyphs, num_glyphs, scaled_font, clip);
    _cairo_rectangle_union (&journal->extents, &entry->base.extents);

    cairo_list_add_tail (&entry->base.link, &journal->entry_list);
    journal->num_entries++;

    return CAIRO_INT_STATUS_SUCCESS;
}

void
_cairo_tg_journal_clear (cairo_tg_journal_t *journal)
{
    cairo_tg_journal_entry_t *entry;
    cairo_tg_journal_entry_t *next;

    cairo_list_foreach_entry_safe (entry, next, cairo_tg_journal_entry_t,
				   &journal->entry_list, link)
    {
	_cairo_tg_journal_entry_fini (entry);
    }

    journal->num_entries = 0;
    cairo_list_init (&journal->entry_list);
    _cairo_tg_mono_allocator_reset(&journal->allocator);
    journal->extents = _cairo_empty_rectangle;
}

cairo_int_status_t
_cairo_tg_journal_replay (const cairo_tg_journal_t		*journal,
			  void					*closure,
			  const cairo_rectangle_int_t		*extents,
			  const cairo_tg_journal_replay_funcs_t	*funcs)
{
    const cairo_tg_journal_entry_t  *entry;
    const cairo_tg_journal_entry_t  *next;
    cairo_int_status_t		    status;

    cairo_list_foreach_entry_safe (entry, next, cairo_tg_journal_entry_t,
				   &journal->entry_list, link)
    {
	if (extents && ! _cairo_rectangle_intersects (extents, &entry->extents))
	{
	    continue;
	}

	switch (entry->type)
	{
	case CAIRO_TG_JOURNAL_ENTRY_PAINT:
	    {
		cairo_tg_journal_entry_paint_t *e =
		    (cairo_tg_journal_entry_paint_t *) entry;

		status = funcs->paint (closure, e->base.op, &e->base.source.base,
				       e->base.clip);
	    }
	    break;
	case CAIRO_TG_JOURNAL_ENTRY_MASK:
	    {
		cairo_tg_journal_entry_mask_t *e =
		    (cairo_tg_journal_entry_mask_t *) entry;

		status = funcs->mask (closure, e->base.op, &e->base.source.base,
				      &e->mask.base, e->base.clip);
	    }
	    break;
	case CAIRO_TG_JOURNAL_ENTRY_STROKE:
	    {
		cairo_tg_journal_entry_stroke_t *e =
		    (cairo_tg_journal_entry_stroke_t *) entry;

		status = funcs->stroke (closure, e->base.op, &e->base.source.base,
					&e->path, &e->style, &e->ctm, &e->ctm_inverse,
					e->tolerance, e->antialias, e->base.clip);
	    }
	    break;
	case CAIRO_TG_JOURNAL_ENTRY_FILL:
	    {
		cairo_tg_journal_entry_fill_t *e =
		    (cairo_tg_journal_entry_fill_t *) entry;

		status = funcs->fill (closure, e->base.op, &e->base.source.base,
				      &e->path, e->fill_rule,
				      e->tolerance, e->antialias, e->base.clip);
	    }
	    break;
	case CAIRO_TG_JOURNAL_ENTRY_GLYPHS:
	    {
		cairo_tg_journal_entry_glyphs_t *e =
		    (cairo_tg_journal_entry_glyphs_t *) entry;

		status = funcs->glyphs (closure, e->base.op, &e->base.source.base,
					e->glyphs, e->num_glyphs,
					e->scaled_font, e->base.clip);
	    }
	    break;
	default:
	    ASSERT_NOT_REACHED;
	    break;
	}

	if (unlikely (status) && status != CAIRO_INT_STATUS_NOTHING_TO_DO)
	{
	    assert (0);
	    return status;
	}
    }

    return CAIRO_INT_STATUS_SUCCESS;
}
