// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>

#define countof(a) (sizeof(a) / sizeof((a)[0]))

#include "float_test_vec.c"

int main(void) {
    printf("floating point printf tests\n");

    for (size_t i = 0; i < float_test_vec_size; i++) {
        PRINT_FLOAT;
    }

    return 0;
}
