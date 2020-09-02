// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>
#include <stdio.h>

#include <openthread/platform/time.h>
uint64_t otPlatTimeGet(void) { return static_cast<uint64_t>(zx_clock_get_monotonic()); }
