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

struct vector *vector_new(void)
{
    struct vector *vector = xmalloc(sizeof(*vector));

    vector->items =
        xcalloc(INITIAL_VECTOR_CAPACITY, sizeof(vector->items[0]));
    vector->capacity = INITIAL_VECTOR_CAPACITY;
    vector->size = 0;

    return vector;
}

void vector_free(struct vector *vector)
{
    free(vector->items);
    free(vector);
}

void vector_add(struct vector *vector, void *data)
{
    if (vector->size == vector->capacity) {
        size_t new_capacity = vector->capacity + (vector->capacity >> 1);
        size_t new_size = new_capacity * sizeof(vector->items[0]);

        vector->items = xrealloc(vector->items, new_size);
        vector->capacity = new_capacity;
    }

    vector->items[vector->size++] = data;
}

void vector_for_each(const struct vector *vector, void (*function)(void *))
{
    size_t item_index;

    for (item_index = 0; item_index < vector->size; ++item_index) {
        function(vector->items[item_index]);
    }
}

void vector_shuffle(struct vector *vector)
{
    static int is_seeded = FALSE;
    size_t item_index;

    if (!is_seeded) {
        srandom(time(NULL) ^ getpid());
        is_seeded = TRUE;
    }

    for (item_index = vector->size - 1; item_index > 0; --item_index) {
        size_t random_item_index = random() % (item_index + 1);
        void *temp = vector->items[item_index];

        vector->items[item_index] = vector->items[random_item_index];
        vector->items[random_item_index] = temp;
    }
}

