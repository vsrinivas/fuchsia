// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_FAKE_TRANSPORT_BASE_FAKE_TRANSPORT_BASE_H_
#define SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_FAKE_TRANSPORT_BASE_FAKE_TRANSPORT_BASE_H_
#include <fuchsia/hardware/test/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define _ALL_SOURCE
#include <fidl/fuchsia.hardware.telephony.transport/cpp/wire.h>
#include <fidl/fuchsia.telephony.snoop/cpp/wire.h>
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
class Device : fidl::WireServer<fuchsia_hardware_telephony_transport::Qmi> {
 public:
  explicit Device(zx_device_t* device);

  virtual zx_status_t Bind() = 0;

  virtual void ReplyCtrlMsg(uint8_t* req, uint32_t req_size, uint8_t* resp, uint32_t resp_size) = 0;
  virtual void SnoopCtrlMsg(uint8_t* snoop_data, uint32_t snoop_data_len,
                            fuchsia_telephony_snoop::wire::Direction direction) = 0;

  zx_status_t FidlDispatch(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx_status_t Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void Unbind();
  void Release();

  zx_status_t GetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t SetChannelToDevice(zx::channel transport);
  zx_status_t SetNetworkStatusToDevice(bool connected);
  zx_status_t SetSnoopChannelToDevice(
      ::fidl::ClientEnd<fuchsia_telephony_snoop::Publisher> channel);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx_status_t CloseCtrlChannel();

  zx_status_t SetAsyncWait();
  zx_status_t EventLoopCleanup();

  // Get/Set functions
  zx::channel& GetCtrlChannel() { return ctrl_channel_; };
  zx::port& GetCtrlChannelPort() { return ctrl_channel_port_; };
  zx::channel& GetCtrlSnoopChannel() { return snoop_client_end_.channel(); };
  zx::port& GetCtrlSnoopChannelPort() { return snoop_port_; };
  std::thread& GetCtrlThrd() { return fake_ctrl_thread_; }

  bool GetConnectStatus() { return connected_; }
  zx_device_t* GetParentDevice() { return parent_; }
  zx_device_t*& GetTelDevPtr() { return tel_dev_; }

 private:
  void SetChannel(SetChannelRequestView request, SetChannelCompleter::Sync& completer) override;
  void SetNetwork(SetNetworkRequestView request, SetNetworkCompleter::Sync& completer) override;
  void SetSnoopChannel(SetSnoopChannelRequestView request,
                       SetSnoopChannelCompleter::Sync& completer) override;

  zx::channel ctrl_channel_;
  zx::port ctrl_channel_port_;
  fidl::ClientEnd<fuchsia_telephony_snoop::Publisher> snoop_client_end_;
  zx::port snoop_port_;
  std::thread fake_ctrl_thread_;
  bool connected_;
  zx_device_t* parent_;
  zx_device_t* tel_dev_;
};

}  // namespace tel_fake

#endif  // SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_FAKE_TRANSPORT_BASE_FAKE_TRANSPORT_BASE_H_
