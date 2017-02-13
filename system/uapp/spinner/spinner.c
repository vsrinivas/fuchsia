// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>

int main(int argc, char** argv) {

    for (uint64_t count = 0; ; count++) {
        if ((count % 1000000000ULL) == 0)
            printf("count %" PRIu64 "\n", count);
    }

    return 0;
}
