// Copyright 2017 The Fuchsia Authors
// All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off

#pragma once

#include <magenta/types.h>

#define BCM_CLOCKMAN_PASSWORD       (uint32_t)( 0x5a000000 )

#define BCM_CLOCKMAN_CONTROL_MASH_INTEGER_DIV       (uint32_t)( 0 << 9 )
#define BCM_CLOCKMAN_CONTROL_MASH_ONE_STAGE         (uint32_t)( 1 << 9 )
#define BCM_CLOCKMAN_CONTROL_MASH_TWO_STAGE         (uint32_t)( 2 << 9 )
#define BCM_CLOCKMAN_CONTROL_MASH_THREE_STAGE       (uint32_t)( 3 << 9 )

#define BCM_CLOCKMAN_CONTROL_FLIP                   (uint32_t)( 1 << 8 )
#define BCM_CLOCKMAN_CONTROL_BUSY                   (uint32_t)( 1 << 7 )
#define BCM_CLOCKMAN_CONTROL_KILL                   (uint32_t)( 1 << 5 )
#define BCM_CLOCKMAN_CONTROL_ENAB                   (uint32_t)( 1 << 4 )

#define BCM_CLOCKMAN_CONTROL_SRC_GND                (uint32_t)( 0 << 0 )
#define BCM_CLOCKMAN_CONTROL_SRC_OSC                (uint32_t)( 1 << 0 )
#define BCM_CLOCKMAN_CONTROL_SRC_DEBUG0             (uint32_t)( 2 << 0 )
#define BCM_CLOCKMAN_CONTROL_SRC_DEBUG1             (uint32_t)( 3 << 0 )
#define BCM_CLOCKMAN_CONTROL_SRC_PLLA               (uint32_t)( 4 << 0 )
#define BCM_CLOCKMAN_CONTROL_SRC_PLLC               (uint32_t)( 5 << 0 )
#define BCM_CLOCKMAN_CONTROL_SRC_PLLD               (uint32_t)( 6 << 0 )
#define BCM_CLOCKMAN_CONTROL_SRC_HDMIAUX            (uint32_t)( 7 << 0 )

#define BCM_CLOCKMAN_PCMCTL                         (0x98)
#define BCM_CLOCKMAN_PCMDIV                         (0x9C)
