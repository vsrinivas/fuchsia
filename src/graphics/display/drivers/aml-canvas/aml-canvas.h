// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AML_CANVAS_AML_CANVAS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AML_CANVAS_AML_CANVAS_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>

#include <array>

#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/amlogiccanvas.h>
#include <fbl/mutex.h>

#define IS_ALIGNED(a, b) (!(((uintptr_t)(a)) & (((uintptr_t)(b)) - 1)))

#define CANVAS_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define CANVAS_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

namespace aml_canvas {

constexpr size_t kNumCanvasEntries = 256;

class AmlCanvas;
using DeviceType = ddk::Device<AmlCanvas, ddk::Unbindable>;

class AmlCanvas : public DeviceType,
                  public ddk::AmlogicCanvasProtocol<AmlCanvas, ddk::base_protocol> {
 public:
  AmlCanvas(zx_device_t* parent, ddk::MmioBuffer mmio, zx::bti bti)
      : DeviceType(parent), dmc_regs_(std::move(mmio)), bti_(std::move(bti)) {}

  // This function is called from the c-bind function upon driver matching
  static zx_status_t Setup(zx_device_t* parent);

  // Required by ddk::AmlogicCanvasProtocol
  zx_status_t AmlogicCanvasConfig(zx::vmo vmo, size_t offset, const canvas_info_t* info,
                                  uint8_t* canvas_idx);
  zx_status_t AmlogicCanvasFree(uint8_t canvas_idx);

  // Required by ddk::Device
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

 private:
  fbl::Mutex lock_;
  ddk::MmioBuffer dmc_regs_ __TA_GUARDED(lock_);
  zx::bti bti_ __TA_GUARDED(lock_);
  std::array<zx::pmt, kNumCanvasEntries> pmts_ __TA_GUARDED(lock_);

};  // class AmlCanvas

}  // namespace aml_canvas

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AML_CANVAS_AML_CANVAS_H_
