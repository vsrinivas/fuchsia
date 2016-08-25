// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

__BEGIN_CDECLS

typedef struct {
    int futex;
} mxr_once_t;

#define MXR_ONCE_INIT {0}

#pragma GCC visibility push(hidden)

// Calls the function exactly once across all threads using the same mxr_once_t.
void mxr_once(mxr_once_t*, void (*)(void));

#pragma GCC visibility pop

__END_CDECLS
