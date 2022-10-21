// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_BIN_ADB_ADB_H_
#define SRC_DEVELOPER_ADB_BIN_ADB_ADB_H_

#include <fidl/fuchsia.hardware.adb/cpp/fidl.h>

#include <map>

#include "service-manager.h"
#include "src/developer/adb/third_party/adb/adb-base.h"
#include "src/developer/adb/third_party/adb/transport.h"

namespace adb {

// DeviceConnector is the base class that connects the component to a device that implements
// fuchsia_hardware_adb::Device.
class DeviceConnector {
 public:
  explicit DeviceConnector() = default;

  // Calls ConnectToDevice() on all available devices, and returns the first one that is able
  // to connect successfully.
  virtual zx::result<fidl::ClientEnd<fuchsia_hardware_adb::Device>> ConnectToFirstDevice() = 0;
};

// Adb connects to devices implementing fuchsia_hardware_adb::Device and calls the Start function to
// get a handle to the fuchsia_hardware_adb::UsbAdbImpl implementation, which allows it to interact
// and transfer packets over USB. Adb also interacts with different ADB services such as shell, ffx,
// and file-sync to connect to it and interact with USB transport.
class Adb : public AdbBase {
 public:
  explicit Adb(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  static zx::result<std::unique_ptr<Adb>> Create(async_dispatcher_t* dispatcher);

  // AdbDaemonBase functions
  bool SendUsbPacket(uint8_t* buf, size_t len) override;
  zx::result<zx::socket> GetServiceSocket(std::string_view service_name,
                                          std::string_view args) override;

 private:
  friend class AdbTest;

  // Starts this implementation by connecting to underlying fuchsia_hardware_adb::UsbAdbImpl and
  // creating required connections
  zx_status_t Init(DeviceConnector* connector);

  // Parses packet received at the end of fuchsia_hardware_adb::UsbAdbImpl::Receive, forwards
  // it to adb-protocol, and sends another fuchsia_hardware_adb::UsbAdbImpl::Receive request
  // for the next packet.
  void ReceiveCallback(fidl::WireUnownedResult<fuchsia_hardware_adb::UsbAdbImpl::Receive>& result);

  async_dispatcher_t* dispatcher_;
  fidl::WireSharedClient<fuchsia_hardware_adb::UsbAdbImpl> impl_;

  // Handle to the third party library implementation of ADB protocol.
  atransport transport_;
  // Sometimes a packet is split up over multiple USB transfers (due to sizing). The next two
  // members, pending_packet_ and copied_len_ keep track of state for packets that require multiple
  // transfers.
  // pending_packet_ keeps track of packets that were not completely received and will be finished
  // on following transfers. If the packet is already complete, pending_packet_ should not hold
  // anything.
  std::unique_ptr<apacket> pending_packet_;
  // copied_len_ keeps track of the current size received of pending_packet_. If packet was
  // completely received, copied_len_ should be 0.
  size_t copied_len_;

  ServiceManager service_manager_;
};

}  // namespace adb

#endif  // SRC_DEVELOPER_ADB_BIN_ADB_ADB_H_
