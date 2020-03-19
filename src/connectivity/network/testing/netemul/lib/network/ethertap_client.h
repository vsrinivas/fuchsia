// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ETHERTAP_CLIENT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ETHERTAP_CLIENT_H_

#include <fuchsia/hardware/ethertap/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>
#include <stdlib.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"

namespace netemul {

class EthertapConfig {
 public:
  std::string name = "etap";
  zx::channel devfs_root;
  fuchsia::hardware::ethertap::Config tap_cfg;

  explicit EthertapConfig(std::string tap_name) : EthertapConfig() {
    name = std::move(tap_name);
    RandomLocalUnicast(name);
  }
  explicit EthertapConfig(const fuchsia::hardware::ethernet::MacAddress& mac) : EthertapConfig() {
    mac.Clone(&tap_cfg.mac);
  }

  EthertapConfig() {
    tap_cfg.features = 0;
    tap_cfg.options = 0;
    tap_cfg.mtu = 1500;
  }

  EthertapConfig(EthertapConfig&& oth);

  // helper to set the unicast bits in the containing tap configuration.
  void SetMacUnicast();
  // helper to set the locally administered bits in the containing tap
  // configuration.
  void SetMacLocallyAdministered();
  // helper function to check if contained mac address is locally administered.
  bool IsMacLocallyAdministered();

  // helper to generate a random local unicast mac with a given string seed
  void RandomLocalUnicast(const std::string& str_seed);

  FXL_DISALLOW_ASSIGN(EthertapConfig);
};

// Helper class to create and operate Ethertap Devices.
// Existence of the tap device is tied to object lifecycle (RAII), so if you
// intend to connect to the ethernet device you must keep the EthertapClient
// instance in scope.
class EthertapClient {
 public:
  using PacketCallback = fit::function<void(std::vector<uint8_t>)>;
  using PeerClosedCallback = fit::function<void()>;
  virtual ~EthertapClient() = default;

  // Toggles ethertap link up signal.
  virtual void SetLinkUp(bool linkUp) = 0;
  // sends data packet.
  virtual zx_status_t Send(std::vector<uint8_t> data) = 0;
  // PacketCallback will be called whenever the tap device receives a packet.
  virtual void SetPacketCallback(PacketCallback cb) = 0;
  // PeerClosedCallback will be called if the client loses connection with the
  // tap device.
  virtual void SetPeerClosedCallback(PeerClosedCallback cb) = 0;
  // Closes connection locally with ethertap (no peer closed cb).
  virtual void Close() = 0;

  virtual zx_status_t Send(const void* data, size_t len) {
    return Send(std::vector<uint8_t>(static_cast<const uint8_t*>(data),
                                     static_cast<const uint8_t*>(data) + len));
  }

  virtual const zx::channel& channel() = 0;

  // Creates an EthertapClient with given configuration.
  // A null reference will be returned if the client can't be created.
  static zx_status_t Create(EthertapConfig config, std::unique_ptr<EthertapClient>* out,
                            async_dispatcher_t* dispatcher = nullptr);
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ETHERTAP_CLIENT_H_
