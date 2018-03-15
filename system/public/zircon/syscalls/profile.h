// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

#define ZX_PROFILE_INFO_SCHEDULER   1

typedef struct zx_profile_scheduler {
    uint32_t priority;
    uint32_t boost;
    uint32_t deboost;
    uint32_t quantum;
} zx_profile_scheduler_t;


typedef struct zx_profile_info {
    uint32_t type;                  // one of ZX_PROFILE_INFO_
    union {
        zx_profile_scheduler_t scheduler;
    };
} zx_profile_info_t;


__END_CDECLS
