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

#include "array.h"
#include "utils.h"

struct array *
array_new(void)
{
	struct array *a;

	a = xmalloc(sizeof(*a));
	a->items = xcalloc(INITIAL_ARRAY_CAPACITY, sizeof(a->items[0]));
	a->cap = INITIAL_ARRAY_CAPACITY;
	a->size = 0;

	return a;
}

void
array_free(struct array *a)
{
	free(a->items);
	free(a);
}

void
array_add(struct array *a, void *data)
{
	if (a->size == a->cap) {
		size_t newcap;

		newcap = a->cap * 3 / 2;
		a->items = xrealloc(a->items, newcap * sizeof(a->items[0]));
		a->cap = newcap;
	}

	a->items[a->size++] = data;
}

void
array_for_each(const struct array *a, void (*f)(void *))
{
	size_t i;

	for (i = 0; i < a->size; ++i)
		f(a->items[i]);
}

void
array_shuffle(struct array *a)
{
	static int seeded = false;
	size_t i;

	if (!seeded) {
		srandom(time(NULL) ^ getpid());
		seeded = true;
	}

	for (i = a->size - 1; i > 0; --i) {
		size_t j;
		void *tmp;

		j = random() % (i + 1);

		tmp = a->items[i];
		a->items[i] = a->items[j];
		a->items[j] = tmp;
	}
}
