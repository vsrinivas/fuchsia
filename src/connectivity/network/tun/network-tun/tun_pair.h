// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_PAIR_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_PAIR_H_

#include <lib/async-loop/cpp/loop.h>

#include <fbl/intrusive_double_list.h>

#include "device_adapter.h"
#include "mac_adapter.h"

namespace network {
namespace tun {

// Implements `fuchsia.net.tun.DevicePair`.
//
// `TunPair` uses `DeviceAdapter` to fulfill the `fuchsia.net.tun.DevicePair` protocol. All FIDL
// requests are served over its own internally held async dispatcher.
class TunPair : public fbl::DoublyLinkedListable<std::unique_ptr<TunPair>>,
                public fidl::WireServer<fuchsia_net_tun::DevicePair>,
                public DeviceAdapterParent {
 public:
  // Creates a new `TunPair` with `config`.
  // `teardown` is called when all the bound client channels are closed.
  static zx::status<std::unique_ptr<TunPair>> Create(
      fit::callback<void(TunPair*)> teardown, fuchsia_net_tun::wire::DevicePairConfig config);
  ~TunPair() override;

  // fuchsia.net.tun.DevicePair implementation:
  void ConnectProtocols(ConnectProtocolsRequestView request,
                        ConnectProtocolsCompleter::Sync& completer) override;

  // DeviceAdapterParent implementation:
  const BaseConfig& config() const override { return config_; };
  void OnHasSessionsChanged(DeviceAdapter* device) override;
  void OnTxAvail(DeviceAdapter* device) override;
  void OnRxAvail(DeviceAdapter* device) override;

  // MacAdapterParent implementation:
  void OnMacStateChanged(MacAdapter* adapter) override;

  // Binds `req` to this device.
  // Requests are served over this device's owned loop.
  // NOTE: at this moment only one binding is supported, if the device is already bound the previous
  // channel is closed.
  void Bind(fidl::ServerEnd<fuchsia_net_tun::DevicePair> req);

 private:
  TunPair(fit::callback<void(TunPair*)> teardown, DevicePairConfig config);
  void ConnectProtocols(DeviceAdapter& device, fuchsia_net_tun::wire::Protocols protos);
  void Teardown();

  fit::callback<void(TunPair*)> teardown_callback_;
  const DevicePairConfig config_;

  fbl::Mutex power_lock_;
  async::Loop loop_;
  std::optional<thrd_t> loop_thread_;
  std::optional<fidl::ServerBindingRef<fuchsia_net_tun::DevicePair>> binding_;
  std::unique_ptr<DeviceAdapter> left_;
  std::unique_ptr<DeviceAdapter> right_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_PAIR_H_
