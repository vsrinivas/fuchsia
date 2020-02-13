// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_OVL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_OVL_H_

#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <memory>
#include <optional>

#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>

#include "common.h"
#include "registers-ovl.h"

namespace mt8167s_display {

// [Ovl] --> [Clr] --> [Clr Correction] --> [AAL] --> [Gamma] --> [Dither] --> [RDMA] --> [DSI]

//
// Overlay class is the first element in the display subsystem. It is responsible
// for fetching pixels from memory, perform blending (up to 4 layers), support RGB and UYVY
// swapping, fixed color conversion coefficient (T601, T709, JPEG), alpha blending and flipping
// (veritical, horizontal, 180-degree flip). The supported memory source formats are as follows:
// RGB565, RGB888, ARGB8888, PARGB8888, XRGB, YUV422
// Single Ovl object will manage all four layers
//
constexpr uint32_t kMaxLayer = 4;

class Ovl {
 public:
  // Contructor
  Ovl(uint32_t height, uint32_t width) : height_(height), width_(width) {
    ZX_ASSERT(height_ < kMaxHeight);
    ZX_ASSERT(width_ < kMaxWidth);
  }

  // Init
  zx_status_t Init(zx_device_t* parent);

  // Stops the overlay engine and resets the active layers
  void Reset();

  // Configure the corresponding layer. Should only be called after Stop has been called
  zx_status_t Config(uint8_t layer, OvlConfig& cfg);

  // Start OVL engine. This will enable interrupts (VSync) and the Overalay engine itself
  void Start();

  // Stop OVL engine. This will place the Overlay engine in Idle mode and safely stop all
  // transactions that may be happening. This function should be called before configuring
  // the Overlay engine with new parameters
  void Stop();

  void Restart() {
    Stop();
    Start();
  }

  // Clears all IRQ sources
  void ClearIrq() { ovl_mmio_->Write32(0x0, OVL_INTSTA); }

  // Returns true if interrupt was not spurious
  bool IsValidIrq() { return ovl_mmio_->Read32(OVL_INTSTA) != 0; }

  // Return true if the input layer is active
  bool IsLayerActive(uint8_t layer) { return ((active_layers_ & (1 << layer)) != 0); }

  // Clears the active layer
  void ClearLayer(uint8_t layer) { active_layers_ &= static_cast<uint8_t>(~(1 << layer)); }

  // Returns the layer handle which is the physical address of the VMO backed image
  zx_paddr_t GetLayerHandle(uint8_t layer) { return layer_handle_[layer]; }

  // Prints the relevant status registers in the Overlay Engine
  void PrintStatusRegisters() {
    DISP_INFO("STA = 0x%x, INTSTA = 0x%x, FLOW_CTRL_DBG = 0x%x\n", ovl_mmio_->Read32(OVL_STA),
              ovl_mmio_->Read32(OVL_INTSTA), ovl_mmio_->Read32(OVL_FLOW_CTRL_DBG));
  }

  // Return true is Overlay Engine is Idle
  bool IsIdle() {
    if ((ovl_mmio_->Read32(OVL_FLOW_CTRL_DBG) & (0x3ff)) != (OVL_IDLE) &&
        (ovl_mmio_->Read32(OVL_FLOW_CTRL_DBG) & (0x3ff)) != (OVL_IDLE2)) {
      return false;
    } else {
      return true;
    }
  }
  void PrintRegisters();

  // Overlay support ARGB, RGB and YUV formats only
  static bool IsSupportedFormat(zx_pixel_format_t format);

 private:
  // Return format as expected by OVL register
  uint32_t GetFormat(zx_pixel_format_t format);

  // BYTE_SWAP: Determines the need for swapping bytes based on format
  bool ByteSwapNeeded(zx_pixel_format_t format);

  // get Bytes per Pixel.
  // TODO(payam): ZX_PIXEL_FORMAT_BYTES returns 4 for x888. We need three
  uint32_t GetBytesPerPixel(zx_pixel_format_t format);

  std::unique_ptr<ddk::MmioBuffer> ovl_mmio_;
  pdev_protocol_t pdev_ = {nullptr, nullptr};
  zx::bti bti_;

  const uint32_t height_;  // Display height
  const uint32_t width_;   // Display width

  uint8_t active_layers_ = 0;
  zx_paddr_t layer_handle_[kMaxLayer] = {};
  bool initialized_ = false;
};

}  // namespace mt8167s_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_OVL_H_
