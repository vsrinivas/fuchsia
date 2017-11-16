// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "private.h"

typedef struct zxsio zxsio_t;

struct zxsio {
    // base fdio io object
    fdio_t io;

    // socket handle
    zx_handle_t s;
};

