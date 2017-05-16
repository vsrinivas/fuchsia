// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxcpp/new.h>

// 512MB structure.
struct BigStruct {
    int y[128 * 1024 * 1024];
};

extern "C" int cpp_out_of_mem() {
    int rv = 0;
    for (int ix = 0; ix < 1000; ++ix) {
        auto big = new BigStruct;
        rv += (&big->y[0] > &rv) ? 0 : 1;
    }
    return rv;
}
