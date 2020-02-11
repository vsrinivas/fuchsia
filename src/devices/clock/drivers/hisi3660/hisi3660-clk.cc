// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/reg.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

#include <zircon/assert.h>
#include <zircon/threads.h>

#include <fbl/array.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clockimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>

#include <dev/clk/hisi-lib/hisi-clk.h>
#include <dev/clk/hisi-lib/hisi-gate.h>
#include <soc/hi3660/hi3660-hw.h>

namespace hisi_clock {

constexpr Gate gates[] = {
    Gate(0x0, 0, RegisterBank::Peri),     Gate(0x0, 21, RegisterBank::Peri),
    Gate(0x0, 30, RegisterBank::Peri),    Gate(0x0, 31, RegisterBank::Peri),
    Gate(0x10, 0, RegisterBank::Peri),    Gate(0x10, 1, RegisterBank::Peri),
    Gate(0x10, 2, RegisterBank::Peri),    Gate(0x10, 3, RegisterBank::Peri),
    Gate(0x10, 4, RegisterBank::Peri),    Gate(0x10, 5, RegisterBank::Peri),
    Gate(0x10, 6, RegisterBank::Peri),    Gate(0x10, 7, RegisterBank::Peri),
    Gate(0x10, 8, RegisterBank::Peri),    Gate(0x10, 9, RegisterBank::Peri),
    Gate(0x10, 10, RegisterBank::Peri),   Gate(0x10, 11, RegisterBank::Peri),
    Gate(0x10, 12, RegisterBank::Peri),   Gate(0x10, 13, RegisterBank::Peri),
    Gate(0x10, 14, RegisterBank::Peri),   Gate(0x10, 15, RegisterBank::Peri),
    Gate(0x10, 16, RegisterBank::Peri),   Gate(0x10, 17, RegisterBank::Peri),
    Gate(0x10, 18, RegisterBank::Peri),   Gate(0x10, 19, RegisterBank::Peri),
    Gate(0x10, 20, RegisterBank::Peri),   Gate(0x10, 21, RegisterBank::Peri),
    Gate(0x10, 30, RegisterBank::Peri),   Gate(0x10, 31, RegisterBank::Peri),
    Gate(0x20, 7, RegisterBank::Peri),    Gate(0x20, 9, RegisterBank::Peri),
    Gate(0x20, 11, RegisterBank::Peri),   Gate(0x20, 12, RegisterBank::Peri),
    Gate(0x20, 14, RegisterBank::Peri),   Gate(0x20, 15, RegisterBank::Peri),
    Gate(0x20, 27, RegisterBank::Peri),   Gate(0x30, 1, RegisterBank::Peri),
    Gate(0x30, 10, RegisterBank::Peri),   Gate(0x30, 11, RegisterBank::Peri),
    Gate(0x30, 12, RegisterBank::Peri),   Gate(0x30, 13, RegisterBank::Peri),
    Gate(0x30, 14, RegisterBank::Peri),   Gate(0x30, 15, RegisterBank::Peri),
    Gate(0x30, 16, RegisterBank::Peri),   Gate(0x30, 17, RegisterBank::Peri),
    Gate(0x30, 28, RegisterBank::Peri),   Gate(0x30, 29, RegisterBank::Peri),
    Gate(0x30, 30, RegisterBank::Peri),   Gate(0x30, 31, RegisterBank::Peri),
    Gate(0x40, 1, RegisterBank::Peri),    Gate(0x40, 4, RegisterBank::Peri),
    Gate(0x40, 17, RegisterBank::Peri),   Gate(0x40, 19, RegisterBank::Peri),
    Gate(0x50, 16, RegisterBank::Peri),   Gate(0x50, 17, RegisterBank::Peri),
    Gate(0x50, 18, RegisterBank::Peri),   Gate(0x50, 21, RegisterBank::Peri),
    Gate(0x50, 28, RegisterBank::Peri),   Gate(0x50, 29, RegisterBank::Peri),
    Gate(0x420, 5, RegisterBank::Peri),   Gate(0x420, 7, RegisterBank::Peri),
    Gate(0x420, 8, RegisterBank::Peri),   Gate(0x420, 9, RegisterBank::Peri),

    Gate(0x258, 7, RegisterBank::Sctrl),  Gate(0x260, 11, RegisterBank::Sctrl),
    Gate(0x260, 12, RegisterBank::Sctrl), Gate(0x260, 13, RegisterBank::Sctrl),
    Gate(0x268, 11, RegisterBank::Sctrl),
};

static_assert(HI3660_SEP_CLK_GATE_COUNT == countof(gates),
              "hi3660_clk_gates[] and hisi_3660_sep_gate_clk_idx count mismatch");

}  // namespace hisi_clock

static const char hi3660_clk_name[] = "hi3660-clk";

static zx_status_t hi3660_clk_bind(void* ctx, zx_device_t* parent) {
  return hisi_clock::HisiClock::Create(hi3660_clk_name, hisi_clock::gates,
                                       countof(hisi_clock::gates), parent);
}

static const zx_driver_ops_t hi3660_clk_driver_ops = [] {
  zx_driver_ops_t result = {};

  result.version = DRIVER_OPS_VERSION;
  result.bind = hi3660_clk_bind;

  return result;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(hi3660_clk, hi3660_clk_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_HI3660_CLK),
ZIRCON_DRIVER_END(hi3660_clk)
