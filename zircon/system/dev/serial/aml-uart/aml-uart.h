// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <ddk/protocol/platform/device.h>
#include <lib/device-protocol/platform-device.h>
#include <ddk/protocol/serialimpl.h>
#include <ddk/protocol/serial.h>
#include <ddktl/device.h>
#include <lib/mmio/mmio.h>
#include <ddktl/protocol/serialimpl.h>

#include <fbl/function.h>
#include <fbl/mutex.h>
#include <lib/zx/interrupt.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/types.h>

#include <utility>

namespace serial {

class AmlUart;
using DeviceType = ddk::Device<AmlUart, ddk::Unbindable>;

class AmlUart : public DeviceType, public ddk::SerialImplProtocol<AmlUart, ddk::base_protocol> {
 public:
  // Spawns device node.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkUnbind() { DdkRemove(); }
  void DdkRelease() {
    SerialImplEnable(false);
    delete this;
  }

  // Serial protocol implementation.
  zx_status_t SerialImplGetInfo(serial_port_info_t* info);
  zx_status_t SerialImplConfig(uint32_t baud_rate, uint32_t flags);
  zx_status_t SerialImplEnable(bool enable);
  zx_status_t SerialImplRead(void* buf, size_t length, size_t* out_actual);
  zx_status_t SerialImplWrite(const void* buf, size_t length, size_t* out_actual);
  zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb);

 private:
  using Callback = fbl::Function<void(uint32_t)>;

  explicit AmlUart(zx_device_t* parent, const pdev_protocol_t& pdev,
                   const serial_port_info_t& serial_port_info, ddk::MmioBuffer mmio)
      : DeviceType(parent),
        pdev_(pdev),
        serial_port_info_(serial_port_info),
        mmio_(std::move(mmio)) {}

  // Reads the current state from the status register and calls notify_cb if it has changed.
  uint32_t ReadStateAndNotify();
  void EnableLocked(bool enable) TA_REQ(enable_lock_);
  int IrqThread();

  const pdev_protocol_t pdev_;
  const serial_port_info_t serial_port_info_;
  ddk::MmioBuffer mmio_;
  zx::interrupt irq_;

  thrd_t irq_thread_ TA_GUARDED(enable_lock_);
  bool enabled_ TA_GUARDED(enable_lock_) = false;

  Callback notify_cb_ TA_GUARDED(status_lock_) = nullptr;
  // Last state we sent to notify_cb.
  uint32_t state_ TA_GUARDED(status_lock_) = 0;

  // Protects enabling/disabling lifecycle.
  fbl::Mutex enable_lock_;
  // Protects status register and notify_cb.
  fbl::Mutex status_lock_;
};

}  // namespace serial
