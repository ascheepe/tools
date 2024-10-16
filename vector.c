/*
 * Copyright (c) 2024 Axel Scheepers
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _XOPEN_SOURCE 600
#include <sys/types.h>
#include <unistd.h>

#include <stdlib.h>
#include <time.h>

#include "vector.h"
#include "utils.h"

struct vector *
vector_new(void)
{
	struct vector *v;

	v = xcalloc(1, sizeof(*v));
	v->items = xcalloc(INITIAL_VECTOR_CAPACITY, sizeof(v->items[0]));
	v->capacity = INITIAL_VECTOR_CAPACITY;
	v->size = 0;

	return v;
}

void
vector_free(struct vector *v)
{
	xfree(v->items);
	v->items = NULL;
	xfree(v);
	v = NULL;
}

void
vector_add(struct vector *v, void *data)
{
	if (v->size == v->capacity) {
		size_t new_capacity = v->capacity + (v->capacity >> 1);
		size_t new_size = new_capacity * sizeof(v->items[0]);

		v->items = xrealloc(v->items, new_size);
		v->capacity = new_capacity;
	}

	v->items[v->size++] = data;
}

void
vector_foreach(const struct vector *v, void (*fn)(void *))
{
	size_t i;

	for (i = 0; i < v->size; ++i)
		fn(v->items[i]);
}

void
vector_shuffle(struct vector *v)
{
	static unsigned int seed;
	size_t i;

	if (seed == 0) {
		seed = time(NULL) ^ getpid();
		srandom(seed);
	}

	for (i = v->size - 1; i > 0; --i) {
		size_t j;
		void *tmp;

		j = random() % (i + 1);

		tmp = v->items[i];
		v->items[i] = v->items[j];
		v->items[j] = tmp;
	}
}
