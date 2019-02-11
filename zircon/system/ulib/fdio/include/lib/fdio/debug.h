// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// BEGIN DEPRECATED ------------------------------------------------------------
// Should be declared in another header somewhere.

// per-file chatty debug macro
#define xprintf(fmt, args...)                                                  \
    do {                                                                       \
        if (ZXDEBUG) {                                                         \
            printf("%s:%d: " fmt, __FILE__, __LINE__, ##args);                 \
        }                                                                      \
    } while (0)

// END DEPRECATED --------------------------------------------------------------

__END_CDECLS
