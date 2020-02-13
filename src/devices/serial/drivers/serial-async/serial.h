// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SERIAL_DRIVERS_SERIAL_ASYNC_SERIAL_H_
#define SRC_DEVICES_SERIAL_DRIVERS_SERIAL_ASYNC_SERIAL_H_

#include <fuchsia/hardware/serial/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/socket.h>
#include <zircon/types.h>

#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/serial.h>
#include <ddktl/protocol/serialimpl/async.h>
#include <fbl/mutex.h>

namespace serial {

class SerialDevice;
using DeviceType = ddk::Device<SerialDevice, ddk::Messageable>;

class SerialDevice : public DeviceType,
                     public llcpp::fuchsia::hardware::serial::NewDevice::Interface,
                     public llcpp::fuchsia::hardware::serial::NewDeviceProxy::Interface {
 public:
  explicit SerialDevice(zx_device_t* parent) : DeviceType(parent), serial_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  zx_status_t Init();

  // Device protocol implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  // Serial protocol implementation.
  zx_status_t SerialGetInfo(serial_port_info_t* info);
  zx_status_t SerialConfig(uint32_t baud_rate, uint32_t flags);
  void Read(ReadCompleter::Sync completer) override;
  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) override;
  void GetChannel(zx::channel req, GetChannelCompleter::Sync completer) override;

  // Fidl protocol implementation.
  void GetClass(GetClassCompleter::Sync completer) override;
  void SetConfig(llcpp::fuchsia::hardware::serial::Config config,
                 SetConfigCompleter::Sync completer) override;

 private:
  // The serial protocol of the device we are binding against.
  ddk::SerialImplAsyncProtocolClient serial_;
  uint32_t serial_class_;
  std::optional<async::Loop> loop_;
  std::optional<ReadCompleter::Async> read_completer_;
  std::optional<WriteCompleter::Async> write_completer_;
};

}  // namespace serial

#endif  // SRC_DEVICES_SERIAL_DRIVERS_SERIAL_ASYNC_SERIAL_H_
