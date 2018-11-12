// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_ETHERNET_CLIENT_H_
#define LIB_NETEMUL_NETWORK_ETHERNET_CLIENT_H_

#include <lib/fzl/fifo.h>
#include <zircon/ethernet/cpp/fidl.h>
#include <memory>
#include "ethertap_types.h"

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

  explicit EthernetClient(fidl::InterfacePtr<zircon::ethernet::Device> ptr);
  ~EthernetClient();
  void Setup(const EthernetConfig& config,
             fit::function<void(zx_status_t)> callback);

  fidl::InterfacePtr<zircon::ethernet::Device>& device() { return device_; }
  fzl::fifo<zircon::ethernet::FifoEntry>& tx_fifo();
  fzl::fifo<zircon::ethernet::FifoEntry>& rx_fifo();

  void SetDataCallback(DataCallback cb);

  void SetPeerClosedCallback(PeerClosedCallback cb);

  zx_status_t Send(const void* data, uint16_t len);
  zx_status_t AcquireAndSend(fit::function<void(void*, uint16_t*)> writer);

  static EthernetClient::Ptr RetrieveWithMAC(const Mac& mac);

 private:
  PeerClosedCallback peer_closed_callback_;
  fidl::InterfacePtr<zircon::ethernet::Device> device_;
  std::unique_ptr<FifoHolder> fifos_;
};

}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_ETHERNET_CLIENT_H_
