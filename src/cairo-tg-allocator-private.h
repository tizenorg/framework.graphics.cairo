#ifndef CAIRO_TG_ALLOCATOR_H
#define CAIRO_TG_ALLOCATOR_H

#include "cairoint.h"

typedef struct _cairo_tg_mem_chunk cairo_tg_mem_chunk_t;

struct _cairo_tg_mem_chunk
{
    cairo_tg_mem_chunk_t    *next;
    uint8_t		    *buffer;
    int			    chunk_size;
    int			    remaining_size;
};

typedef struct _cairo_tg_mono_allocator
{
   cairo_tg_mem_chunk_t	    *chunk_head;
   int			    chunk_size;
} cairo_tg_mono_allocator_t;

static inline cairo_tg_mem_chunk_t *
_cairo_tg_mem_chunk_create (int chunk_size)
{
    cairo_tg_mem_chunk_t *chunk;

    chunk = (cairo_tg_mem_chunk_t *) malloc (sizeof (cairo_tg_mem_chunk_t) + chunk_size);

    if (chunk)
    {
	chunk->next = NULL;
	chunk->buffer = (uint8_t *) chunk + sizeof (cairo_tg_mem_chunk_t);
	chunk->chunk_size = chunk_size;
	chunk->remaining_size = chunk_size;
    }

    return chunk;
}

static inline void
_cairo_tg_mem_chunk_destroy (cairo_tg_mem_chunk_t *chunk)
{
    free (chunk);
}

static inline cairo_status_t
_cairo_tg_mono_allocator_init (cairo_tg_mono_allocator_t *allocator, int chunk_size)
{
    cairo_tg_mem_chunk_t *chunk;

    chunk = _cairo_tg_mem_chunk_create (chunk_size);

    if (! chunk)
	return CAIRO_STATUS_NO_MEMORY;

    allocator->chunk_size = chunk_size;
    allocator->chunk_head = chunk;

    return CAIRO_STATUS_SUCCESS;
}

static inline void
_cairo_tg_mono_allocator_fini (cairo_tg_mono_allocator_t *allocator)
{
    cairo_tg_mem_chunk_t *chunk = allocator->chunk_head, *next;

    while (chunk != NULL)
    {
	next = chunk->next;
	_cairo_tg_mem_chunk_destroy (chunk);
	chunk = next;
    }

    allocator->chunk_head = NULL;
}

static inline void *
_cairo_tg_mono_allocator_alloc (cairo_tg_mono_allocator_t *allocator, int size)
{
    cairo_tg_mem_chunk_t *chunk = allocator->chunk_head;
    int chunk_size;

    if (chunk && chunk->remaining_size >= size)
    {
	void *buffer = (void*)(chunk->buffer + chunk->chunk_size - chunk->remaining_size);
	chunk->remaining_size -= size;
	return buffer;
    }

    chunk_size = MAX (allocator->chunk_size, size);

    chunk = _cairo_tg_mem_chunk_create (chunk_size);

    if (chunk == NULL)
	return NULL;

    chunk->next = allocator->chunk_head;
    chunk->buffer = (uint8_t *) chunk + sizeof (cairo_tg_mem_chunk_t);
    chunk->chunk_size = chunk_size;
    chunk->remaining_size = chunk_size - size;

    allocator->chunk_head = chunk;

    return (void *) chunk->buffer;
}

static inline void
_cairo_tg_mono_allocator_reset (cairo_tg_mono_allocator_t *allocator)
{
    cairo_tg_mem_chunk_t *chunk = allocator->chunk_head, *next;
    cairo_tg_mem_chunk_t *stock = NULL;

    while (chunk != NULL)
    {
	next = chunk->next;

	if (stock)
	    _cairo_tg_mem_chunk_destroy (chunk);
	else
	    stock = chunk;

	chunk = next;
    }

    if (stock)
    {
	stock->next = NULL;
	stock->remaining_size = stock->chunk_size;
    }

    allocator->chunk_head = stock;
}

#endif /* CAIRO_TG_ALLOCATOR_H */
