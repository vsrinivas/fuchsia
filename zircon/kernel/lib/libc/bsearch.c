// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014, Google Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <stdlib.h>

void *bsearch(const void *key, const void *base, size_t num_elems, size_t size,
              int (*compare)(const void *, const void *))
{
    size_t low = 0, high = num_elems - 1;

    if (num_elems == 0) {
        return NULL;
    }

    for (;;) {
        size_t mid = low + ((high - low) / 2);
        const void *mid_elem = ((unsigned char *) base) + mid*size;
        int r = compare(key, mid_elem);

        if (r < 0) {
            if (mid == 0) {
                return NULL;
            }
            high = mid - 1;
        } else if (r > 0) {
            low = mid + 1;
            if (low < mid || low > high) {
                return NULL;
            }
        } else {
            return (void *) mid_elem;
        }
    }
}
