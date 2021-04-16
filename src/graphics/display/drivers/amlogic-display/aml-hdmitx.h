// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_

#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/hdmi/llcpp/fidl.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <string.h>

#include <optional>

#include <fbl/auto_lock.h>

#include "common.h"

namespace amlogic_display {

using fuchsia_hardware_hdmi::wire::DisplayMode;

// TODO(fxb/69026): move HDMI to its own device
class AmlHdmitx {
 public:
  explicit AmlHdmitx(ddk::PDev pdev) : pdev_(pdev) {}

  zx_status_t Init();
  zx_status_t InitHw();
  zx_status_t InitInterface(const DisplayMode& mode);

  void WriteReg(uint32_t addr, uint32_t data);
  uint32_t ReadReg(uint32_t addr);

  void ShutDown() {}  // no-op. Shut down handled by phy

  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count);

 private:
  struct hdmi_param_tx {
    uint16_t vic;
    uint8_t aspect_ratio;
    uint8_t colorimetry;
    bool is4K;
  };
  void CalculateTxParam(const DisplayMode& mode, hdmi_param_tx* p);

  void ConfigHdmitx(const DisplayMode& mode, const hdmi_param_tx& p);
  void ConfigCsc(const DisplayMode& mode);

  void ScdcWrite(uint8_t addr, uint8_t val);
  void ScdcRead(uint8_t addr, uint8_t* val);

  ddk::PDev pdev_;

  fbl::Mutex register_lock_;
  std::optional<ddk::MmioBuffer> hdmitx_mmio_ TA_GUARDED(register_lock_);

  fbl::Mutex i2c_lock_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_
