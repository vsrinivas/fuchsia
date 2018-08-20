// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

typedef struct meson_clk_gate {
    uint32_t reg; // Offset from Clock Base Addr (in 4 byte words)
    uint32_t bit; // Offset into this register.
} meson_clk_gate_t;
