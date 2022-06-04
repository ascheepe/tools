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

struct vector *vector_new(void) {
    struct vector *vector = xmalloc(sizeof(*vector));

    vector->items = xcalloc(
            INITIAL_VECTOR_CAPACITY,
            sizeof(vector->items[0])
    );
    vector->capacity = INITIAL_VECTOR_CAPACITY;
    vector->size = 0;

    return vector;
}

void vector_free(struct vector *vector) {
    free(vector->items);
    free(vector);
}

void vector_add(struct vector *vector, void *data) {
    if (vector->size == vector->capacity) {
        size_t new_capacity = vector->capacity + (vector->capacity >> 1);

        vector->items = xrealloc(
                vector->items,
                new_capacity * sizeof(vector->items[0])
        );
        vector->capacity = new_capacity;
    }

    vector->items[vector->size++] = data;
}

void vector_for_each(const struct vector *vector, void (*function)(void *)) {
    size_t i;

    for (i = 0; i < vector->size; ++i) {
        function(vector->items[i]);
    }
}

void vector_shuffle(struct vector *vector) {
    static int is_seeded = FALSE;
    size_t i;

    if (!is_seeded) {
        srandom(time(NULL) ^ getpid());
        is_seeded = TRUE;
    }

    for (i = vector->size - 1; i > 0; --i) {
        size_t j = random() % (i + 1);
        void *tmp = vector->items[i];

        vector->items[i] = vector->items[j];
        vector->items[j] = tmp;
    }
}

