// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_CTL_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_CTL_H_

#include <fuchsia/net/tun/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "tun_device.h"
#include "tun_pair.h"

namespace network {
namespace tun {

// Implements `fuchsia.net.tun.Control`.
//
// `TunCtl` is created with a `dispatcher`, over which it serves the `fuchsia.net.tun.Control`
// protocol. It retains lists of created `TunDevice`s and `TunPair`s.
class TunCtl : public fuchsia::net::tun::Control {
 public:
  TunCtl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void Connect(fidl::InterfaceRequest<fuchsia::net::tun::Control> req) {
    bindings_set_.AddBinding(this, std::move(req), dispatcher_);
  }

  fidl::InterfaceRequestHandler<fuchsia::net::tun::Control> GetHandler() {
    return
        [this](fidl::InterfaceRequest<fuchsia::net::tun::Control> req) { Connect(std::move(req)); };
  }

  void CreateDevice(fuchsia::net::tun::DeviceConfig config,
                    fidl::InterfaceRequest<fuchsia::net::tun::Device> device) override;
  void CreatePair(fuchsia::net::tun::DevicePairConfig config,
                  fidl::InterfaceRequest<fuchsia::net::tun::DevicePair> device_pair) override;

  // Schedules `shutdown_callback` to be called once all devices and device pairs are torn down and
  // destroyed.
  //
  // NOTE: This does not trigger the destruction of all devices, it installs an observer that
  // will notify the caller when all the devices are destroyed by their regular lifetime semantics.
  void SetSafeShutdownCallback(fit::callback<void()> shutdown_callback);

 private:
  void TryFireShutdownCallback();
  async_dispatcher_t* dispatcher_;
  fit::callback<void()> shutdown_callback_;
  fidl::BindingSet<fuchsia::net::tun::Control> bindings_set_;
  fbl::DoublyLinkedList<std::unique_ptr<TunDevice>> devices_;
  fbl::DoublyLinkedList<std::unique_ptr<TunPair>> device_pairs_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_CTL_H_
