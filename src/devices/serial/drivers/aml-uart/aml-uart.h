// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SERIAL_DRIVERS_AML_UART_AML_UART_H_
#define SRC_DEVICES_SERIAL_DRIVERS_AML_UART_AML_UART_H_

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/serial/c/banjo.h>
#include <fuchsia/hardware/serialimpl/async/c/banjo.h>
#include <fuchsia/hardware/serialimpl/async/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fit/function.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/types.h>

#include <utility>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <soc/aml-common/aml-pwm-regs.h>

namespace serial {

class AmlUart;
using DeviceType = ddk::Device<AmlUart>;

class AmlUart : public DeviceType,
                public ddk::SerialImplAsyncProtocol<AmlUart, ddk::base_protocol> {
 public:
  // Spawns device node.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease() {
    SerialImplAsyncEnable(false);
    delete this;
  }

  // Serial protocol implementation.
  zx_status_t SerialImplAsyncGetInfo(serial_port_info_t* info);
  zx_status_t SerialImplAsyncConfig(uint32_t baud_rate, uint32_t flags);
  zx_status_t SerialImplAsyncEnable(bool enable);
  void SerialImplAsyncCancelAll();
  void SerialImplAsyncReadAsync(serial_impl_async_read_async_callback callback, void* cookie);
  void SerialImplAsyncWriteAsync(const uint8_t* buf_buffer, size_t buf_size,
                                 serial_impl_async_write_async_callback callback, void* cookie);

  zx_status_t Init();

  explicit AmlUart(zx_device_t* parent, const ddk::PDev& pdev,
                   const serial_port_info_t& serial_port_info, fdf::MmioBuffer mmio)
      : DeviceType(parent),
        pdev_(pdev),
        serial_port_info_(serial_port_info),
        mmio_(std::move(mmio)) {}

  // Test functions: simulate a data race where the HandleTX / HandleRX functions get called twice.
  void HandleTXRaceForTest();
  void HandleRXRaceForTest();

 private:
  using Callback = fit::function<void(uint32_t)>;

  // Reads the current state from the status register and calls notify_cb if it has changed.
  uint32_t ReadStateAndNotify();
  uint32_t ReadState();
  void EnableLocked(bool enable) TA_REQ(enable_lock_);
  int IrqThread();
  void HandleRX();
  void HandleTX();
  fit::closure MakeReadCallbackLocked(zx_status_t status, void* buf, size_t len) TA_REQ(read_lock_);
  fit::closure MakeWriteCallbackLocked(zx_status_t status) TA_REQ(write_lock_);

  ddk::PDev pdev_;
  const serial_port_info_t serial_port_info_;
  fdf::MmioBuffer mmio_;
  zx::interrupt irq_;

  thrd_t irq_thread_ TA_GUARDED(enable_lock_);
  bool enabled_ TA_GUARDED(enable_lock_) = false;

  Callback notify_cb_ TA_GUARDED(status_lock_) = nullptr;

  // Protects enabling/disabling lifecycle.
  fbl::Mutex enable_lock_;
  // Protects status register and notify_cb.
  fbl::Mutex status_lock_;
  fbl::Mutex read_lock_;
  bool read_pending_ TA_GUARDED(read_lock_) = false;
  serial_impl_async_read_async_callback read_callback_ TA_GUARDED(read_lock_) = nullptr;
  void* read_cookie_ TA_GUARDED(read_lock_) = nullptr;

  fbl::Mutex write_lock_;
  bool write_pending_ TA_GUARDED(write_lock_) = false;
  serial_impl_async_write_async_callback write_callback_ TA_GUARDED(write_lock_) = nullptr;
  void* write_cookie_ TA_GUARDED(write_lock_) = nullptr;
  const uint8_t* write_buffer_ TA_GUARDED(write_lock_) = nullptr;
  size_t write_size_ TA_GUARDED(write_lock_) = 0;
};

}  // namespace serial

#endif  // SRC_DEVICES_SERIAL_DRIVERS_AML_UART_AML_UART_H_
