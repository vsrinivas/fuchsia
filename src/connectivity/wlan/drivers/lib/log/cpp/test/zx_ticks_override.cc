// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/syscalls.h>

static zx_ticks_t g_current_ticks = 0;

__EXPORT zx_ticks_t zx_ticks_get() { return g_current_ticks; }
void zx_ticks_set(zx_ticks_t ticks) { g_current_ticks = ticks; }
void zx_ticks_increment(zx_ticks_t ticks) { g_current_ticks += ticks; }
