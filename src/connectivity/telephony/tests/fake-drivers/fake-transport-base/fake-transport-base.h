// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_FAKE_TRANSPORT_BASE_FAKE_TRANSPORT_BASE_H_
#define SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_FAKE_TRANSPORT_BASE_FAKE_TRANSPORT_BASE_H_
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>

#define _ALL_SOURCE
#include <fuchsia/hardware/telephony/transport/llcpp/fidl.h>
#include <fuchsia/telephony/snoop/llcpp/fidl.h>
#include <lib/zx/port.h>

#include <thread>

namespace tel_fake {

// port info
typedef enum {
  kChannelMsg = 1,
  kInterruptMsg,
  kTerminateMsg,
} DevicePacketEnum;

// TODO (jiamingw): change the name of FIDL protocol in next CL.
class Device : ::llcpp::fuchsia::hardware::telephony::transport::Qmi::Interface {
 public:
  explicit Device(zx_device_t* device);

  virtual zx_status_t Bind() = 0;

  virtual void ReplyCtrlMsg(uint8_t* req, uint32_t req_size, uint8_t* resp, uint32_t resp_size) = 0;
  virtual void SnoopCtrlMsg(uint8_t* snoop_data, uint32_t snoop_data_len,
                            ::llcpp::fuchsia::telephony::snoop::Direction direction) = 0;

  zx_status_t FidlDispatch(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t Message(fidl_msg_t* msg, fidl_txn_t* txn);
  void Unbind();
  void Release();

  zx_status_t GetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t SetChannelToDevice(zx::channel transport);
  zx_status_t SetNetworkStatusToDevice(bool connected);
  zx_status_t SetSnoopChannelToDevice(zx::channel channel);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t CloseCtrlChannel();

  zx_status_t SetAsyncWait();
  zx_status_t EventLoopCleanup();

  // Get/Set functions
  zx::channel& GetCtrlChannel() { return ctrl_channel_; };
  zx::port& GetCtrlChannelPort() { return ctrl_channel_port_; };
  zx::channel& GetCtrlSnoopChannel() { return snoop_channel_; };
  zx::port& GetCtrlSnoopChannelPort() { return snoop_channel_port_; };
  std::thread& GetCtrlThrd() { return fake_ctrl_thread_; }

  bool GetConnectStatus() { return connected_; }
  zx_device_t* GetParentDevice() { return parent_; }
  zx_device_t*& GetTelDevPtr() { return tel_dev_; }

 private:
  void SetChannel(::zx::channel transport, SetChannelCompleter::Sync& completer) override;
  void SetNetwork(bool connected, SetNetworkCompleter::Sync& completer) override;
  void SetSnoopChannel(::zx::channel interface, SetSnoopChannelCompleter::Sync& completer) override;

  zx::channel ctrl_channel_;
  zx::port ctrl_channel_port_;
  zx::channel snoop_channel_;
  zx::port snoop_channel_port_;
  std::thread fake_ctrl_thread_;
  bool connected_;
  zx_device_t* parent_;
  zx_device_t* tel_dev_;
};

}  // namespace tel_fake

#endif  // SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_FAKE_TRANSPORT_BASE_FAKE_TRANSPORT_BASE_H_
