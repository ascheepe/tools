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

struct array *new_array(void) {
    struct array *array;

    array = xmalloc(sizeof(*array));
    array->items = xcalloc(INITIAL_ARRAY_CAPACITY, sizeof(array->items[0]));
    array->capacity = INITIAL_ARRAY_CAPACITY;
    array->size = 0;

    return array;
}

void free_array(struct array *array) {
    free(array->items);
    free(array);
}

void add_to_array(struct array *array, void *data) {
    if (array->size == array->capacity) {
        size_t new_capacity;

        new_capacity = array->capacity * 3 / 2;
        array->items = xrealloc(array->items,
                new_capacity * sizeof(array->items[0]));
        array->capacity = new_capacity;
    }

    array->items[array->size++] = data;
}

void for_each_array_item(const struct array *array, void (*function)(void *)) {
    size_t item_nr;

    for (item_nr = 0; item_nr < array->size; ++item_nr) {
        function(array->items[item_nr]);
    }
}

void shuffle_array(struct array *array) {
    static int is_seeded = false;
    size_t item_nr;

    if (!is_seeded) {
        srandom(time(NULL) ^ getpid());
        is_seeded = true;
    }

    for (item_nr = array->size - 1; item_nr > 0; --item_nr) {
        size_t random_item_nr;
        void *tmp;

        random_item_nr = random() % (item_nr + 1);

        tmp = array->items[item_nr];
        array->items[item_nr] = array->items[random_item_nr];
        array->items[random_item_nr] = tmp;
    }
}

