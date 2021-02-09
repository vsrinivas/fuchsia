// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SERIAL_DRIVERS_UART16550_UART16550_H_
#define SRC_DEVICES_SERIAL_DRIVERS_UART16550_UART16550_H_

#include <fuchsia/hardware/acpi/cpp/banjo.h>
#include <fuchsia/hardware/serialimpl/cpp/banjo.h>
#include <lib/zx/fifo.h>
#include <zircon/compiler.h>

#include <mutex>
#include <thread>
#include <variant>
#include <vector>

#include <ddktl/device.h>
#include <fbl/function.h>
#include <hwreg/bitfields.h>
#include <hwreg/pio.h>

#if UART16550_TESTING
#include <hwreg/mock.h>
#endif

namespace uart16550 {

class Uart16550;
using DeviceType = ddk::Device<Uart16550, ddk::Unbindable>;

class Uart16550 : public DeviceType, public ddk::SerialImplProtocol<Uart16550, ddk::base_protocol> {
 public:
  Uart16550();

  explicit Uart16550(zx_device_t* parent);

  size_t FifoDepth() const;

  bool Enabled();

  bool NotifyCallbackSet();

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t Init();

#if UART16550_TESTING
  // test-use only
  zx_status_t Init(zx::interrupt interrupt, hwreg::Mock::RegisterIo port_mock);
#endif

  // test-use only
  zx::unowned_interrupt InterruptHandle();

  // ddk::SerialImplProtocol
  zx_status_t SerialImplGetInfo(serial_port_info_t* info);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplConfig(uint32_t baud_rate, uint32_t flags);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplEnable(bool enable);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplRead(void* buf, size_t size, size_t* actual);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplWrite(const void* buf, size_t size, size_t* actual);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb);

  // ddk::Releasable
  void DdkRelease();

  // ddk::Unbindable
  void DdkUnbind(ddk::UnbindTxn txn);

 private:
  bool SupportsAutomaticFlowControl() const;

  void ResetFifosLocked() __TA_REQUIRES(device_mutex_);

  void InitFifosLocked() __TA_REQUIRES(device_mutex_);

  void NotifyLocked() __TA_REQUIRES(device_mutex_);

  void HandleInterrupts();

  ddk::AcpiProtocolClient acpi_;
  std::mutex device_mutex_;

  std::thread interrupt_thread_;
  zx::interrupt interrupt_;

  serial_notify_t notify_cb_ __TA_GUARDED(device_mutex_) = {};

#if UART16550_TESTING
  // This should never be used before Init, but must be default-constructible.
  // The Mock is the default (first) variant so it's default-constructible.
  std::variant<hwreg::Mock::RegisterIo, hwreg::RegisterPio> port_io_ __TA_GUARDED(device_mutex_){
      std::in_place_index<0>};
#else
  std::variant<hwreg::RegisterPio> port_io_ __TA_GUARDED(device_mutex_){nullptr};
#endif

  size_t uart_fifo_len_ = 1;

  bool enabled_ __TA_GUARDED(device_mutex_) = false;
  serial_state_t state_ __TA_GUARDED(device_mutex_) = 0;
};  // namespace uart16550

}  // namespace uart16550

#endif  // SRC_DEVICES_SERIAL_DRIVERS_UART16550_UART16550_H_
