/*
 * Copyright (c) 2021 Axel Scheepers
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

	v = xmalloc(sizeof(*v));
	v->items = xcalloc(INITIAL_VECTOR_CAPACITY, sizeof(v->items[0]));
	v->cap = INITIAL_VECTOR_CAPACITY;
	v->size = 0;

	return v;
}

void
vector_free(struct vector *v)
{
	free(v->items);
	free(v);
}

void
vector_add(struct vector *v, void *data)
{
	if (v->size == v->cap) {
		size_t newcap = v->cap + (v->cap >> 1);

		v->items = xrealloc(v->items, newcap * sizeof(v->items[0]));
		v->cap = newcap;
	}

	v->items[v->size++] = data;
}

void
vector_foreach(const struct vector *v, void (*f)(void *))
{
	size_t i;

	for (i = 0; i < v->size; ++i)
		f(v->items[i]);
}

void
vector_shuffle(struct vector *v)
{
	static int is_seeded = FALSE;
	size_t i;

	if (!is_seeded) {
		srandom(time(NULL) ^ getpid());
		is_seeded = TRUE;
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

