// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SERIAL_DRIVERS_SERIAL_ASYNC_SERIAL_H_
#define SRC_DEVICES_SERIAL_DRIVERS_SERIAL_ASYNC_SERIAL_H_

#include <fuchsia/hardware/serial/cpp/banjo.h>
#include <fuchsia/hardware/serial/llcpp/fidl.h>
#include <fuchsia/hardware/serialimpl/async/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/socket.h>
#include <zircon/types.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/mutex.h>

namespace serial {

class SerialDevice;
using DeviceType =
    ddk::Device<SerialDevice, ddk::Messageable<fuchsia_hardware_serial::NewDeviceProxy>::Mixin>;

class SerialDevice : public DeviceType,
                     public fidl::WireServer<fuchsia_hardware_serial::NewDevice>,
                     public fidl::WireServer<fuchsia_hardware_serial::NewDeviceProxy> {
 public:
  explicit SerialDevice(zx_device_t* parent) : DeviceType(parent), serial_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  zx_status_t Init();

  // Device protocol implementation.
  void DdkRelease();

  // Serial protocol implementation.
  zx_status_t SerialGetInfo(serial_port_info_t* info);
  zx_status_t SerialConfig(uint32_t baud_rate, uint32_t flags);
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override;
  void GetChannel(GetChannelRequestView request, GetChannelCompleter::Sync& completer) override;

  // Fidl protocol implementation.
  void GetClass(GetClassRequestView request, GetClassCompleter::Sync& completer) override;
  void SetConfig(SetConfigRequestView request, SetConfigCompleter::Sync& completer) override;

 private:
  // The serial protocol of the device we are binding against.
  ddk::SerialImplAsyncProtocolClient serial_;
  uint32_t serial_class_;
  std::optional<async::Loop> loop_;
  std::optional<ReadCompleter::Async> read_completer_;
  std::optional<WriteCompleter::Async> write_completer_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_serial::NewDevice>> binding_;
  sync_completion_t on_unbind_;  // Signaled on Unbind() to allow DdkRelease() to proceed.
};

}  // namespace serial

#endif  // SRC_DEVICES_SERIAL_DRIVERS_SERIAL_ASYNC_SERIAL_H_
