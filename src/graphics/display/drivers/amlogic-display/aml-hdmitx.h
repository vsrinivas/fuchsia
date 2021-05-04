// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_

#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/hdmi/llcpp/fidl.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/hdmi-dw/hdmi-dw.h>
#include <lib/hdmi/base.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <string.h>

#include <optional>

#include <fbl/auto_lock.h>

#include "common.h"

namespace amlogic_display {

using fuchsia_hardware_hdmi::wire::DisplayMode;

// TODO(fxb/69026): move HDMI to its own device
class AmlHdmitx : HdmiIpBase {
 public:
  explicit AmlHdmitx(ddk::PDev pdev) : pdev_(pdev), hdmi_dw_(this) {}

  zx_status_t Init();
  zx_status_t InitHw();
  zx_status_t InitInterface(const DisplayMode& mode);

  void WriteReg(uint32_t addr, uint32_t data);
  uint32_t ReadReg(uint32_t addr);

  void ShutDown() {}  // no-op. Shut down handled by phy

  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count);

 private:
  void WriteIpReg(uint32_t addr, uint32_t data) final {
    fbl::AutoLock lock(&register_lock_);
    hdmitx_mmio_->Write8(data, addr);
  }
  uint32_t ReadIpReg(uint32_t addr) final {
    fbl::AutoLock lock(&register_lock_);
    return hdmitx_mmio_->Read8(addr);
  }

  void CalculateTxParam(const DisplayMode& mode, hdmi_dw::hdmi_param_tx* p);

  void ScdcWrite(uint8_t addr, uint8_t val);
  void ScdcRead(uint8_t addr, uint8_t* val);

  ddk::PDev pdev_;
  hdmi_dw::HdmiDw hdmi_dw_;

  fbl::Mutex register_lock_;
  std::optional<ddk::MmioBuffer> hdmitx_mmio_ TA_GUARDED(register_lock_);

  fbl::Mutex i2c_lock_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMITX_H_
