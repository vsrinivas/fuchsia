// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/net/ppp/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zx/channel.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/serial.h>
#include <fbl/span.h>

#include "lib/common/ppp.h"
#include "lib/hdlc/frame.h"

namespace ppp {

class SerialPpp;
using DeviceType = ddk::Device<SerialPpp, ddk::Messageable>;

class SerialPpp final : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_PPP> {
 public:
  SerialPpp();

  explicit SerialPpp(zx_device_t* parent);

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t Init();

  // test-use only
  void WaitCallbacks();

  // ddk::Releasable
  void DdkRelease();

  // ddk::Unbindable
  void DdkUnbind(ddk::UnbindTxn txn);

  // ddk::Messageable
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  // fuchsia.connectivity.ppp.Device
  // For ProtocolType::Control, the first two bytes of data encode (in network
  // byte order) the PPP control protocol.
  void Rx(llcpp::fuchsia::net::ppp::ProtocolType protocol,
          fit::callback<void(fit::result<Frame, zx_status_t>)> callback);

  // fuchsia.connectivity.ppp.Device
  // For ProtocolType::Control, the first two bytes of data encode (in network
  // byte order) the PPP control protocol.
  zx_status_t Tx(llcpp::fuchsia::net::ppp::ProtocolType protocol, fbl::Span<const uint8_t> data);

  // fuchsia.connectivity.ppp.Device
  llcpp::fuchsia::net::ppp::Info GetInfo();

  // fuchsia.connectivity.ppp.Device
  bool GetStatus(llcpp::fuchsia::net::ppp::ProtocolType protocol);

  // fuchsia.connectivity.ppp.Device
  void SetStatus(llcpp::fuchsia::net::ppp::ProtocolType protocol, bool up);

  // fuchsia.connectivity.ppp.Device
  zx_status_t Enable(bool up);

  // test-use only
  zx_status_t Enable(bool up, zx::socket socket);

  // fuchsia.connectivity.ppp.DeviceBootstrap
  zx::channel GetInstance();

 private:
  class FrameDeviceServer final : public llcpp::fuchsia::net::ppp::Device::Interface {
   public:
    explicit FrameDeviceServer(SerialPpp* dev);

    void Rx(llcpp::fuchsia::net::ppp::ProtocolType protocol, RxCompleter::Sync completer) override;

    void Tx(llcpp::fuchsia::net::ppp::ProtocolType protocol, fidl::VectorView<uint8_t> data,
            TxCompleter::Sync completer) override;

    void GetInfo(GetInfoCompleter::Sync completer) override;

    void GetStatus(llcpp::fuchsia::net::ppp::ProtocolType protocol,
                   GetStatusCompleter::Sync completer) override;

    void SetStatus(llcpp::fuchsia::net::ppp::ProtocolType protocol, bool up,
                   SetStatusCompleter::Sync completer) override;

    void Enable(bool up, EnableCompleter::Sync completer) override;

   private:
    SerialPpp* dev_ = nullptr;
  };

  class FrameDeviceBootstrapServer final
      : public llcpp::fuchsia::net::ppp::DeviceBootstrap::Interface {
   public:
    explicit FrameDeviceBootstrapServer(SerialPpp* dev);

    void GetInstance(GetInstanceCompleter::Sync completer) override;

   private:
    SerialPpp* dev_ = nullptr;
  };

  void RxLoop();

  fit::result<Frame, zx_status_t> ReadFramed(std::vector<uint8_t>* raw_frame);

  zx_status_t WriteFramed(FrameView frame);

  ddk::SerialProtocolClient serial_protocol_;
  zx::socket serial_;
  FrameDeviceServer server_ = FrameDeviceServer(this);
  FrameDeviceBootstrapServer bootstrap_server_ = FrameDeviceBootstrapServer(this);

  std::atomic_bool enabled_ = false;

  std::thread rx_thread_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  fit::callback<void(fit::result<Frame, zx_status_t>)> ipv4_callback_;
  fit::callback<void(fit::result<Frame, zx_status_t>)> ipv6_callback_;
  fit::callback<void(fit::result<Frame, zx_status_t>)> control_callback_;

  std::mutex write_mutex_;
  std::mutex ipv4_mutex_;
  std::mutex ipv6_mutex_;
  std::mutex control_mutex_;

  std::queue<Frame> ipv4_frames_;
  std::queue<Frame> ipv6_frames_;
  std::queue<Frame> control_frames_;

  std::atomic_bool ipv4_up_ = false;
  std::atomic_bool ipv6_up_ = false;
};

}  // namespace ppp
