// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ETHERNET_CLIENT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ETHERNET_CLIENT_H_

#include <fuchsia/hardware/ethernet/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <zircon/device/ethernet.h>

#include <memory>

namespace netemul {

struct EthernetConfig {
  // number of fifo bufs
  uint16_t nbufs;
  // size of fifo bufs
  uint16_t buff_size;
};

class FifoHolder;

class EthernetClient {
 public:
  using Ptr = std::unique_ptr<EthernetClient>;
  using DataCallback = fit::function<void(const void* buf, size_t len)>;
  using PeerClosedCallback = fit::function<void()>;
  using LinkStatusChangedCallback = fit::function<void(bool link_up)>;
  using Mac = fuchsia::hardware::ethernet::MacAddress;

  explicit EthernetClient(
      async_dispatcher_t* dispatcher,
      fidl::InterfacePtr<fuchsia::hardware::ethernet::Device> ptr);
  ~EthernetClient();

  // Configures the ethernet client with given configuration.
  // Clients need to call Setup before they're able to receive any data from
  // this client.
  void Setup(const EthernetConfig& config,
             fit::function<void(zx_status_t)> callback);

  fidl::InterfacePtr<fuchsia::hardware::ethernet::Device>& device() {
    return device_;
  }

  // DataCallback will be called for every ethernet packet received by the FIFO
  void SetDataCallback(DataCallback cb);
  // PeerClosed callback will be called if the underlying ethernet devices
  // disappears or there's an error with the FIDL interface
  void SetPeerClosedCallback(PeerClosedCallback cb);

  void SetLinkStatusChangedCallback(LinkStatusChangedCallback cb);

  // Send a packet to the interface.
  zx_status_t Send(const void* data, uint16_t len);
  // Acquires a fifo buffer and sends data.
  // Clients that care about performance should prefer this over Send.
  zx_status_t AcquireAndSend(fit::function<void(void*, uint16_t*)> writer);

  // Returns whether the last observed device status signal
  // says that the device is online.
  bool online() const;

 private:
  void set_online(bool online);

  async_dispatcher_t* dispatcher_;
  PeerClosedCallback peer_closed_callback_;
  LinkStatusChangedCallback link_status_changed_callback_;
  bool online_;
  fidl::InterfacePtr<fuchsia::hardware::ethernet::Device> device_;
  std::unique_ptr<FifoHolder> fifos_;
};

class EthernetClientFactory {
 public:
  explicit EthernetClientFactory(std::string root = "/dev/class/ethernet")
      : base_dir_(std::move(root)) {}

  // finds the mount point of an ethernet device with given Mac.
  // This is achieved based on directory watching and will only fail on timeout.
  std::string MountPointWithMAC(const EthernetClient::Mac& mac,
                                unsigned int deadline_ms = 2000);

  // Same as MountPointWithMac, but returns an instance of EthernetClient
  // already bound to the found mount point.
  EthernetClient::Ptr RetrieveWithMAC(const EthernetClient::Mac& mac,
                                      unsigned int deadline_ms = 2000,
                                      async_dispatcher_t* dispatcher = nullptr);

 private:
  const std::string base_dir_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ETHERNET_CLIENT_H_
