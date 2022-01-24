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

struct array *array_new(void) {
    struct array *array;

    array = xmalloc(sizeof(*array));
    array->items = xcalloc(INITIAL_ARRAY_CAPACITY, sizeof(array->items[0]));
    array->capacity = INITIAL_ARRAY_CAPACITY;
    array->size = 0;

    return array;
}

void array_free(struct array *array) {
    free(array->items);
    free(array);
}

void array_add(struct array *array, void *data) {
    if (array->size == array->capacity) {
        size_t new_capacity;

        new_capacity = array->capacity * 3 / 2;
        array->items = xrealloc(array->items,
                new_capacity * sizeof(array->items[0]));
        array->capacity = new_capacity;
    }

    array->items[array->size++] = data;
}

void array_for_each(const struct array *array, void (*function)(void *)) {
    size_t i;

    for (i = 0; i < array->size; ++i) {
        function(array->items[i]);
    }
}

void array_shuffle(struct array *array) {
    static int is_seeded = false;
    size_t i;

    if (!is_seeded) {
        srandom(time(NULL) ^ getpid());
        is_seeded = true;
    }

    for (i = array->size - 1; i > 0; --i) {
        size_t j;
        void *tmp;

        j = random() % (i + 1);

        tmp = array->items[i];
        array->items[i] = array->items[j];
        array->items[j] = tmp;
    }
}

