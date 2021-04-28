// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SERIAL_DRIVERS_SERIAL_SERIAL_H_
#define SRC_DEVICES_SERIAL_DRIVERS_SERIAL_SERIAL_H_

#include <fuchsia/hardware/serial/cpp/banjo.h>
#include <fuchsia/hardware/serial/llcpp/fidl.h>
#include <fuchsia/hardware/serialimpl/cpp/banjo.h>
#include <lib/ddk/driver.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/socket.h>
#include <zircon/types.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/mutex.h>

namespace serial {

class SerialDevice;
using DeviceType = ddk::Device<SerialDevice, ddk::Openable, ddk::Closable, ddk::Readable,
                               ddk::Writable, ddk::Messageable>;

class SerialDevice : public DeviceType,
                     public fidl::WireServer<fuchsia_hardware_serial::Device>,
                     public ddk::SerialProtocol<SerialDevice, ddk::base_protocol> {
 public:
  explicit SerialDevice(zx_device_t* parent) : DeviceType(parent), serial_(parent), open_(false) {}

  static zx_status_t Create(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  zx_status_t Init();

  // Device protocol implementation.
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags);
  zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual);
  zx_status_t DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  // Serial protocol implementation.
  zx_status_t SerialGetInfo(serial_port_info_t* info);
  zx_status_t SerialConfig(uint32_t baud_rate, uint32_t flags);
  zx_status_t SerialOpenSocket(zx::socket* out_handle);

 private:
  zx_status_t WorkerThread();
  void StateCallback(serial_state_t state);

  // Fidl protocol implementation.
  void GetClass(GetClassRequestView request, GetClassCompleter::Sync& completer) override;
  void SetConfig(SetConfigRequestView request, SetConfigCompleter::Sync& completer) override;

  // The serial protocol of the device we are binding against.
  ddk::SerialImplProtocolClient serial_;

  zx::socket socket_;  // socket used for communicating with our client
  zx::event event_;    // event for signaling serial driver state changes

  fbl::Mutex lock_;
  thrd_t thread_;
  uint32_t serial_class_;
  bool open_ TA_GUARDED(lock_);
};

}  // namespace serial

#endif  // SRC_DEVICES_SERIAL_DRIVERS_SERIAL_SERIAL_H_
