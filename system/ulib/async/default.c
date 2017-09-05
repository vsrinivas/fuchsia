// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/default.h>

#include <threads.h>

static thread_local async_t* g_default;

async_t* async_get_default(void) {
    return g_default;
}

void async_set_default(async_t* async) {
    g_default = async;
}
