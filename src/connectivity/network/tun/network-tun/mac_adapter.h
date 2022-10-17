// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_MAC_ADAPTER_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_MAC_ADAPTER_H_

#include <fidl/fuchsia.net.tun/cpp/wire.h>
#include <fidl/fuchsia.net/cpp/wire.h>

#include <fbl/mutex.h>

#include "src/connectivity/network/drivers/network-device/mac/public/network_mac.h"
#include "state.h"

namespace network {
namespace tun {

class MacAdapter;

// An abstract MacAdapter parent.
//
// This abstract class allows the owner of a `MacAdapter` to be notified of important events.
class MacAdapterParent {
 public:
  virtual ~MacAdapterParent() = default;

  // Called when there are changes to the internal state of the `adapter`.
  virtual void OnMacStateChanged(MacAdapter* adapter) = 0;
};

// An entity that instantiates a MacAddrDeviceInterface and provides an implementations of
// `fuchsia.hardware.network.device.MacAddr` that grants access to the requested operating state
// of its interface.
//
// `MacAdapter` is used to provide the business logic of virtual MacAddr implementations both for
// `tun.Device` and `tun.DevicePair` device classes.
class MacAdapter : public ddk::MacAddrProtocol<MacAdapter>, public MacAddrDeviceInterface {
 public:
  // Creates a new `MacAdapter` with `parent`.
  // `mac` is the device's own MAC address, reported to the MacAddrDeviceInterface.
  // if `promisc_only` is true, the only filtering mode reported to the interface will be
  // `MODE_PROMISCUOUS`.
  // On success, the adapter is stored in `out`.
  static zx::result<std::unique_ptr<MacAdapter>> Create(MacAdapterParent* parent,
                                                        fuchsia_net::wire::MacAddress mac,
                                                        bool promisc_only);
  // Binds the request channel to the MacAddrDeviceInterface. Requests will be served over the
  // provided `dispatcher`.
  zx_status_t Bind(async_dispatcher_t* dispatcher,
                   fidl::ServerEnd<netdev::MacAddressing> req) override;
  // Tears down this adapter and calls `callback` when teardown is finished.
  // Tearing down causes all client channels to be closed.
  // There are no guarantees over which thread `callback` is called.
  // It is invalid to attempt to tear down an adapter that is already tearing down or is already
  // torn down.
  void Teardown(fit::callback<void()> callback) override;
  // Same as `Teardown`, but blocks until teardown is complete.
  void TeardownSync();

  const fuchsia_net::wire::MacAddress& mac() { return mac_; }

  // MacAddr protocol:
  void MacAddrGetAddress(uint8_t out_mac[MAC_SIZE]);
  void MacAddrGetFeatures(features_t* out_features);
  void MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list, size_t multicast_macs_count);

  MacState GetMacState();
  mac_addr_protocol_t proto() { return {.ops = &mac_addr_protocol_ops_, .ctx = this}; }

 private:
  MacAdapter(MacAdapterParent* parent, fuchsia_net::wire::MacAddress mac, bool promisc_only)
      : parent_(parent), mac_(mac), promisc_only_(promisc_only) {}

  fbl::Mutex state_lock_;
  std::unique_ptr<MacAddrDeviceInterface> device_;
  MacAdapterParent* const parent_;  // pointer to parent, not owned.
  fuchsia_net::wire::MacAddress mac_;
  const bool promisc_only_;
  MacState mac_state_ __TA_GUARDED(state_lock_);
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_MAC_ADAPTER_H_
