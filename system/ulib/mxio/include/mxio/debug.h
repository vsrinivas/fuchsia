// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <system/compiler.h>
#include <stdio.h>

__BEGIN_CDECLS

// raw console printf, to go away before long
void cprintf(const char* fmt, ...);

// per-file chatty debug macro
#define xprintf(fmt...)   \
    do {                  \
        if (MXDEBUG) {    \
            cprintf(fmt); \
        }                 \
    } while (0)

__END_CDECLS
