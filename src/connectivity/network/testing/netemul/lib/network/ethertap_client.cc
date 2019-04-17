// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethertap_client.h"

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/hardware/ethertap/cpp/fidl.h>
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
#include <random>

static const char kTapctl[] = "/dev/misc/tapctl";

#define MAC_LOCAL (0x02)
#define MAC_MULTICAST (0x01)

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
      async_dispatcher_t* dispatcher, EthertapConfig config) {
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

    zx_status_t o_status = ZX_OK;

    status = tapctl->OpenDevice(config.name, config.tap_cfg,
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

    return std::make_unique<EthertapClientImpl>(std::move(tapdevice),
                                                std::move(config));
  }

  void Close() override { device_.Unbind(); }

  const zx::channel& channel() override { return device_.channel(); }

  EthertapConfig config_;
  fidl::InterfacePtr<TapDevice> device_;
  PacketCallback packet_callback_;
  PeerClosedCallback peer_closed_callback_;
};

std::unique_ptr<EthertapClient> EthertapClient::Create(
    EthertapConfig config, async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  return EthertapClientImpl::Create(dispatcher, std::move(config));
}

void EthertapConfig::RandomLocalUnicast(const std::string& str_seed) {
  std::vector<uint8_t> sseed(str_seed.begin(), str_seed.end());
  std::random_device rd;
  // Add some randomness to the name from random_device
  // as a temporary fix due to ethertap devfs entries being leaked
  // across test boundaries (which caused tests to fail).
  // TODO(brunodalbo) go back to only the string seed
  //  once ZX-2956 is fixed.
  sseed.push_back(rd());
  sseed.push_back(rd());
  sseed.push_back(rd());
  sseed.push_back(rd());
  std::seed_seq seed(sseed.begin(), sseed.end());
  std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t>
      rnd(seed);
  std::generate(tap_cfg.mac.octets.begin(), tap_cfg.mac.octets.end(), rnd);
  // set unicast:
  SetMacUnicast();
  SetMacLocallyAdministered();
}

void EthertapConfig::SetMacUnicast() {
  tap_cfg.mac.octets[0] &= ~(MAC_MULTICAST);
}

void EthertapConfig::SetMacLocallyAdministered() {
  tap_cfg.mac.octets[0] |= MAC_LOCAL;
}

bool EthertapConfig::IsMacLocallyAdministered() {
  return (tap_cfg.mac.octets[0] & MAC_LOCAL) != 0;
}

}  // namespace netemul