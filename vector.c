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

struct vector *new_vector(void) {
    struct vector *vector;

    vector = xmalloc(sizeof(*vector));
    vector->items = xcalloc(INITIAL_VECTOR_CAPACITY,
                            sizeof(vector->items[0]));
    vector->capacity = INITIAL_VECTOR_CAPACITY;
    vector->size = 0;

    return vector;
}

void free_vector(struct vector *vector) {
    free(vector->items);
    free(vector);
}

void add_to_vector(struct vector *vector, void *data) {
    if (vector->size == vector->capacity) {
        size_t new_capacity;

        new_capacity = vector->capacity * 3 / 2;
        vector->items = xrealloc(vector->items,
                                 new_capacity * sizeof(vector->items[0]));
        vector->capacity = new_capacity;
    }

    vector->items[vector->size++] = data;
}

void for_each_vector_item(const struct vector *vector,
                          void (*function)(void *)) {
    size_t item_nr;

    for (item_nr = 0; item_nr < vector->size; ++item_nr) {
        function(vector->items[item_nr]);
    }
}

void shuffle_vector(struct vector *vector) {
    static int is_seeded = false;
    size_t item_nr;

    if (!is_seeded) {
        srandom(time(NULL) ^ getpid());
        is_seeded = true;
    }

    for (item_nr = vector->size - 1; item_nr > 0; --item_nr) {
        size_t random_item_nr;
        void *tmp;

        random_item_nr = random() % (item_nr + 1);

        tmp = vector->items[item_nr];
        vector->items[item_nr] = vector->items[random_item_nr];
        vector->items[random_item_nr] = tmp;
    }
}

