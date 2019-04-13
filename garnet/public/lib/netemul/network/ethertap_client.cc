// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethertap_client.h"

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>

static const char kTapctl[] = "/dev/misc/tapctl";

namespace netemul {

class EthertapClientImpl : public EthertapClient {
 public:
  using TapDevice = fuchsia::hardware::ethertap::TapDevice;
  using TapControl = fuchsia::hardware::ethertap::TapControl;
  explicit EthertapClientImpl(fidl::InterfacePtr<TapDevice> device,
                              EthertapConfig config)
      : config_(std::move(config)), device_(std::move(device)) {
    device_.events().OnFrame = [this](std::vector<uint8_t> data) {
      if (packet_callback_) {
        packet_callback_(std::move(data));
      }
    };

    device_.set_error_handler([this](zx_status_t status) {
      fprintf(stderr, "Ethertap device error: %s\n",
              zx_status_get_string(status));
      if (peer_closed_callback_) {
        peer_closed_callback_();
      }
    });
  }

  void SetLinkUp(bool linkUp) override { device_->SetOnline(linkUp); }

  zx_status_t Send(std::vector<uint8_t> data) override {
    device_->WriteFrame(std::move(data));
    return ZX_OK;
  }

  void SetPacketCallback(PacketCallback cb) override {
    packet_callback_ = std::move(cb);
  }

  void SetPeerClosedCallback(PeerClosedCallback cb) override {
    peer_closed_callback_ = std::move(cb);
  }

  static std::unique_ptr<EthertapClientImpl> Create(
      async_dispatcher_t* dispatcher, const EthertapConfig& incfg) {
    zx::socket sock;

    fidl::SynchronousInterfacePtr<TapControl> tapctl;

    auto status = fdio_service_connect(
        kTapctl, tapctl.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      fprintf(stderr, "could not open %s: %s\n", kTapctl,
              zx_status_get_string(status));
      return nullptr;
    }

    fidl::InterfacePtr<TapDevice> tapdevice;

    fuchsia::hardware::ethertap::Config config;
    config.features = 0;
    config.options = incfg.options;
    config.mtu = incfg.mtu;
    memcpy(config.mac.octets.data(), incfg.mac.d, 6);
    zx_status_t o_status = ZX_OK;

    status = tapctl->OpenDevice(incfg.name, std::move(config),
                                tapdevice.NewRequest(dispatcher), &o_status);
    if (status != ZX_OK) {
      fprintf(stderr, "Could not open tap device: %s\n",
              zx_status_get_string(status));
      return nullptr;
    } else if (o_status != ZX_OK) {
      fprintf(stderr, "Could not open tap device: %s\n",
              zx_status_get_string(o_status));
      return nullptr;
    }

    return std::make_unique<EthertapClientImpl>(std::move(tapdevice), incfg);
  }

  void Close() override { device_.Unbind(); }

  const zx::channel& channel() override { return device_.channel(); }

  EthertapConfig config_;
  fidl::InterfacePtr<TapDevice> device_;
  PacketCallback packet_callback_;
  PeerClosedCallback peer_closed_callback_;
};

std::unique_ptr<EthertapClient> EthertapClient::Create(
    const EthertapConfig& config, async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  return EthertapClientImpl::Create(dispatcher, config);
}

}  // namespace netemul