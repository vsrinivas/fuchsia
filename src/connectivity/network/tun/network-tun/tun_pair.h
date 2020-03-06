// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_PAIR_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_PAIR_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>

#include <fbl/intrusive_double_list.h>

#include "device_adapter.h"
#include "mac_adapter.h"

namespace network {
namespace tun {

// Implements `fuchsia.net.tun.DevicePair`.
//
// `TunPair` uses `DeviceAdapter` and `MacAdapter` to fulfill the `fuchsia.net.tun.DevicePair`
// protocol. All FIDL requests are served over its own internally held AsyncLoop.
class TunPair : public fbl::DoublyLinkedListable<std::unique_ptr<TunPair>>,
                public fuchsia::net::tun::DevicePair,
                public DeviceAdapterParent,
                public MacAdapterParent {
 public:
  // Creates a new `TunPair` with `config`.
  // `teardown` is called when all the bound client channels are closed.
  // On success, the new device is stored in `out`.
  static zx_status_t Create(fit::callback<void(TunPair*)> teardown,
                            fuchsia::net::tun::DevicePairConfig config,
                            std::unique_ptr<TunPair>* out);
  ~TunPair() override;

  // fuchsia.net.tun.DevicePair implementation:
  void ConnectProtocols(fuchsia::net::tun::DevicePairEnds requests) override;

  // DeviceAdapterParent implementation:
  const fuchsia::net::tun::BaseConfig& config() const override { return config_.base(); };
  void OnHasSessionsChanged(DeviceAdapter* device) override;
  void OnTxAvail(DeviceAdapter* device) override;
  void OnRxAvail(DeviceAdapter* device) override;

  // MacAdapterParent implementation:
  void OnMacStateChanged(MacAdapter* adapter) override;

  // Binds `req` to this device.
  // Requests are served over this device's owned loop.
  // NOTE: at this moment only one binding is supported, if the device is already bound the previous
  // channel is closed.
  void Bind(fidl::InterfaceRequest<fuchsia::net::tun::DevicePair> req);

 private:
  TunPair(fit::callback<void(TunPair*)> teardown, fuchsia::net::tun::DevicePairConfig config);
  void ConnectProtocols(const std::unique_ptr<DeviceAdapter>& device,
                        const std::unique_ptr<MacAdapter>& mac,
                        fuchsia::net::tun::Protocols protos);
  void Teardown();

  fbl::Mutex power_lock_;
  fit::callback<void(TunPair*)> teardown_callback_;
  fuchsia::net::tun::DevicePairConfig config_;
  async::Loop loop_;
  fit::optional<thrd_t> loop_thread_;
  fidl::Binding<fuchsia::net::tun::DevicePair> binding_;
  std::unique_ptr<DeviceAdapter> left_;
  std::unique_ptr<DeviceAdapter> right_;
  std::unique_ptr<MacAdapter> mac_left_;
  std::unique_ptr<MacAdapter> mac_right_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_PAIR_H_
