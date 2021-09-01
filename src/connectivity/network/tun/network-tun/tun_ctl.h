// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_CTL_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_CTL_H_

#include <fidl/fuchsia.net.tun/cpp/wire.h>

#include "tun_device.h"
#include "tun_pair.h"

namespace network {
namespace tun {

// Forward declaration for test-only friend class used below.
namespace testing {
class TunTest;
}

// Implements `fuchsia.net.tun.Control`.
//
// `TunCtl` is created with a `dispatcher`, over which it serves the `fuchsia.net.tun.Control`
// protocol. It retains lists of created `TunDevice`s and `TunPair`s.
class TunCtl : public fidl::WireServer<fuchsia_net_tun::Control> {
 public:
  TunCtl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void Connect(fidl::ServerEnd<fuchsia_net_tun::Control> req) {
    fidl::BindServer(dispatcher_, std::move(req), this);
  }

  void CreateDevice(CreateDeviceRequestView request,
                    CreateDeviceCompleter::Sync& completer) override;
  void CreatePair(CreatePairRequestView request, CreatePairCompleter::Sync& completer) override;

  // Schedules `shutdown_callback` to be called once all devices and device pairs are torn down and
  // destroyed.
  //
  // NOTE: This does not trigger the destruction of all devices, it installs an observer that
  // will notify the caller when all the devices are destroyed by their regular lifetime semantics.
  void SetSafeShutdownCallback(fit::callback<void()> shutdown_callback);

 protected:
  friend testing::TunTest;
  const fbl::DoublyLinkedList<std::unique_ptr<TunDevice>>& devices() const { return devices_; }

 private:
  void TryFireShutdownCallback();
  async_dispatcher_t* dispatcher_;
  fit::callback<void()> shutdown_callback_;
  fbl::DoublyLinkedList<std::unique_ptr<TunDevice>> devices_;
  fbl::DoublyLinkedList<std::unique_ptr<TunPair>> device_pairs_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_TUN_CTL_H_
