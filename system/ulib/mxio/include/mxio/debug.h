// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stdio.h>

__BEGIN_CDECLS

// per-file chatty debug macro
#define xprintf(fmt...)   \
    do {                  \
        if (MXDEBUG) {    \
            printf(fmt); \
        }                 \
    } while (0)

__END_CDECLS
