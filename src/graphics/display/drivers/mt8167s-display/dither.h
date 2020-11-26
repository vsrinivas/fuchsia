// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_DITHER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_DITHER_H_
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <hwreg/mmio.h>

#include "common.h"
#include "registers-dither.h"

namespace mt8167s_display {

// [Ovl] --> [Color] --> [CCorr] --> [AAL] --> [Gamma] --> [Dither] --> [RDMA] --> [DSI]
//
// Dither engine decreases the RGB depth while reducing loss of quality due to
// quantization errors

class Dither {
 public:
  Dither(uint32_t height, uint32_t width) : height_(height), width_(width) {
    ZX_ASSERT(height_ < kMaxHeight);
    ZX_ASSERT(width_ < kMaxWidth);
  }
  zx_status_t Init(ddk::PDev& pdev);
  zx_status_t Config();
  void PrintRegisters();

 private:
  std::optional<ddk::MmioBuffer> dither_mmio_;
  const uint32_t height_;  // Display height
  const uint32_t width_;   // Display width
  bool initialized_ = false;
};

}  // namespace mt8167s_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_DITHER_H_
