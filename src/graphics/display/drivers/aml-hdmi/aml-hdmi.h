// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AML_HDMI_AML_HDMI_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AML_HDMI_AML_HDMI_H_

#include <fidl/fuchsia.hardware.hdmi/cpp/wire.h>
#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/hdmi/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/device-protocol/pdev.h>
#include <lib/hdmi-dw/hdmi-dw.h>
#include <lib/hdmi/base.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <string.h>

#include <optional>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>

#define DISPLAY_MASK(start, count) (((1 << (count)) - 1) << (start))
#define DISPLAY_SET_MASK(mask, start, count, value) \
  (((mask) & ~DISPLAY_MASK(start, count)) | (((value) << (start)) & DISPLAY_MASK(start, count)))

#define SET_BIT32(x, dest, value, start, count)                                    \
  WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~DISPLAY_MASK(start, count)) | \
                              (((value) << (start)) & DISPLAY_MASK(start, count)))

#define GET_BIT32(x, dest, start, count) \
  ((READ32_##x##_REG(dest) >> (start)) & ((1 << (count)) - 1))

#define SET_MASK32(x, dest, mask) WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) | (mask)))

#define CLEAR_MASK32(x, dest, mask) WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~(mask)))

#define WRITE32_REG(x, a, v) WRITE32_##x##_REG(a, v)
#define READ32_REG(x, a) READ32_##x##_REG(a)

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_SPEW(fmt, ...) zxlogf(TRACE, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE zxlogf(INFO, "[%s %d]", __func__, __LINE__)

namespace aml_hdmi {

class AmlHdmiDevice;
using DeviceType = ddk::Device<AmlHdmiDevice, ddk::Messageable<fuchsia_hardware_hdmi::Hdmi>::Mixin,
                               ddk::Unbindable>;

class AmlHdmiDevice : public DeviceType,
                      public ddk::HdmiProtocol<AmlHdmiDevice, ddk::base_protocol>,
                      public HdmiIpBase,
                      public fbl::RefCounted<AmlHdmiDevice> {
 public:
  explicit AmlHdmiDevice(zx_device_t* parent)
      : DeviceType(parent),
        HdmiIpBase(),
        pdev_(parent),
        hdmi_dw_(std::make_unique<hdmi_dw::HdmiDw>(this)),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  zx_status_t Bind();

  void DdkUnbind(ddk::UnbindTxn txn) {
    loop_.Shutdown();
    txn.Reply();
  }
  void DdkRelease() { delete this; }

  void HdmiConnect(zx::channel chan);

  void PowerUp(PowerUpRequestView request, PowerUpCompleter::Sync& completer) {
    ZX_DEBUG_ASSERT(request->display_id == 1);  // only supports 1 display for now
    // no-op. initialization handled in modeset
    completer.ReplySuccess();
  }
  void PowerDown(PowerDownRequestView request, PowerDownCompleter::Sync& completer) {
    // no-op. handled by phy
    ZX_DEBUG_ASSERT(request->display_id == 1);  // only supports 1 display for now
    completer.Reply();
  }
  void IsPoweredUp(IsPoweredUpRequestView request, IsPoweredUpCompleter::Sync& completer) {
    ZX_DEBUG_ASSERT(request->display_id == 1);  // only supports 1 display for now
    completer.Reply(is_powered_up_);
  }
  void Reset(ResetRequestView request, ResetCompleter::Sync& completer);
  void ModeSet(ModeSetRequestView request, ModeSetCompleter::Sync& completer);
  void EdidTransfer(EdidTransferRequestView request, EdidTransferCompleter::Sync& completer);
  void WriteReg(WriteRegRequestView request, WriteRegCompleter::Sync& completer) {
    WriteReg(request->reg, request->val);
    completer.Reply();
  }
  void ReadReg(ReadRegRequestView request, ReadRegCompleter::Sync& completer) {
    auto val = ReadReg(request->reg);
    completer.Reply(val);
  }
  void EnableBist(EnableBistRequestView request, EnableBistCompleter::Sync& completer) {
    ZX_DEBUG_ASSERT(request->display_id == 1);  // only supports 1 display for now
    completer.ReplySuccess();
  }
  void PrintHdmiRegisters(PrintHdmiRegistersCompleter::Sync& completer);

 private:
  friend class FakeAmlHdmiDevice;
  enum {
    MMIO_HDMI,
  };

  // For unit testing
  AmlHdmiDevice(zx_device_t* parent, fdf::MmioBuffer mmio, std::unique_ptr<hdmi_dw::HdmiDw> hdmi_dw)
      : DeviceType(parent),
        pdev_(parent),
        hdmi_dw_(std::move(hdmi_dw)),
        hdmitx_mmio_(std::move(mmio)),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void WriteIpReg(uint32_t addr, uint32_t data) {
    fbl::AutoLock lock(&register_lock_);
    hdmitx_mmio_->Write8(data, addr);
  }
  uint32_t ReadIpReg(uint32_t addr) {
    fbl::AutoLock lock(&register_lock_);
    return hdmitx_mmio_->Read8(addr);
  }
  void WriteReg(uint32_t reg, uint32_t val);
  uint32_t ReadReg(uint32_t reg);

  void PrintReg(std::string name, uint8_t reg);

  ddk::PDev pdev_;
  fbl::Mutex dw_lock_;
  std::unique_ptr<hdmi_dw::HdmiDw> hdmi_dw_ TA_GUARDED(dw_lock_);

  fbl::Mutex register_lock_;
  std::optional<fdf::MmioBuffer> hdmitx_mmio_ TA_GUARDED(register_lock_);

  bool is_powered_up_ = false;
  bool loop_started_ = false;
  async::Loop loop_;
};

}  // namespace aml_hdmi

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AML_HDMI_AML_HDMI_H_
