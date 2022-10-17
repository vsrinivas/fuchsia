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
  static zx::result<std::unique_ptr<TunPair>> Create(
      fit::callback<void(TunPair*)> teardown, fuchsia_net_tun::wire::DevicePairConfig config);
  ~TunPair() override;

  // DeviceAdapterParent implementation:
  const BaseDeviceConfig& config() const override { return config_; }
  void OnTxAvail(DeviceAdapter* device) override;
  void OnRxAvail(DeviceAdapter* device) override;

  // Binds `req` to this device.
  // Requests are served over this device's owned loop.
  // NOTE: at this moment only one binding is supported, if the device is already bound the previous
  // channel is closed.
  void Bind(fidl::ServerEnd<fuchsia_net_tun::DevicePair> req);

  void AddPort(AddPortRequestView request, AddPortCompleter::Sync& completer) override;
  void RemovePort(RemovePortRequestView request, RemovePortCompleter::Sync& completer) override;
  void GetLeft(GetLeftRequestView request, GetLeftCompleter::Sync& _completer) override;
  void GetRight(GetRightRequestView request, GetRightCompleter::Sync& _completer) override;
  void GetLeftPort(GetLeftPortRequestView request, GetLeftPortCompleter::Sync& _completer) override;
  void GetRightPort(GetRightPortRequestView request,
                    GetRightPortCompleter::Sync& _completer) override;

 private:
  class Port : public PortAdapterParent {
   public:
    Port(Port&&) = delete;
    static zx::result<std::unique_ptr<Port>> Create(
        TunPair* parent, bool left, const BasePortConfig& config,
        std::optional<fuchsia_net::wire::MacAddress> mac);
    // MacAdapterParent implementation:
    void OnMacStateChanged(MacAdapter* adapter) override;

    // PortAdapterParent implementation:
    void OnHasSessionsChanged(PortAdapter& port) override;
    void OnPortStatusChanged(PortAdapter& port, const port_status_t& new_status) override;
    void OnPortDestroyed(PortAdapter& port) override;

    PortAdapter& adapter() { return *adapter_; }

   private:
    Port(TunPair* parent, bool left) : parent_(parent), left_(left) {}
    TunPair* const parent_;
    const bool left_;
    std::unique_ptr<PortAdapter> adapter_;
  };
  struct Ports {
    std::unique_ptr<Port> left;
    std::unique_ptr<Port> right;
  };

  TunPair(fit::callback<void(TunPair*)> teardown, DevicePairConfig config);
  void Teardown();

  fit::callback<void(TunPair*)> teardown_callback_;
  const DevicePairConfig config_;

  fbl::Mutex power_lock_;
  std::array<Ports, MAX_PORTS> ports_ __TA_GUARDED(power_lock_);

  async::Loop loop_;
  std::optional<thrd_t> loop_thread_;
  std::optional<fidl::ServerBindingRef<fuchsia_net_tun::DevicePair>> binding_;
  std::unique_ptr<DeviceAdapter> left_;
  std::unique_ptr<DeviceAdapter> right_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_PAIR_H_
