// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <zircon/types.h>

#define HISI_CLK_FLAG_BANK_MASK  (0x7)
#define HISI_CLK_FLAG_BANK(v) ((v) & HISI_CLK_FLAG_BANK_MASK)
#define     HISI_CLK_FLAG_BANK_SCTRL (0x1)
#define     HISI_CLK_FLAG_BANK_PERI  (0x2)


typedef struct hisi_clk_gate {
    uint32_t reg;   // Offset from Clock Base Addr (in 4 byte words)
    uint32_t bit;   // Offset into this register.

    uint32_t flags;
} hisi_clk_gate_t;

// Initialize a hisilicon clock
zx_status_t hisi_clk_init(const char* name, hisi_clk_gate_t* gates,
                          const size_t gate_count, zx_device_t* parent);
