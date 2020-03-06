// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_MAC_ADAPTER_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_MAC_ADAPTER_H_

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/tun/cpp/fidl.h>
#include <lib/fidl-async/cpp/bind.h>

#include <fbl/mutex.h>

#include "src/connectivity/network/drivers/network-device/mac/public/network_mac.h"

namespace network {
namespace tun {

class MacAdapter;

// An abstract MacAdapter parent.
//
// This abstract class allows the owner of a `MacAdapter` to be notified of important events.
class MacAdapterParent {
 public:
  // Called when there are changes to the internal state of the `adapter`.
  virtual void OnMacStateChanged(MacAdapter* adapter) = 0;
};

// An entity that instantiates a MacAddrDeviceInterface and provides an implementations of
// `ddk.protocol.network.device.MacAddrImpl` that grants access to the requested operating state of
// its interface.
//
// `MacAdapter` is used to provide the business logic of virtual MacAddr implementations both for
// `tun.Device` and `tun.DevicePair` device classes.
class MacAdapter : public ddk::MacAddrImplProtocol<MacAdapter>, public MacAddrDeviceInterface {
 public:
  // Creates a new `MacAdapter` with `parent`.
  // `mac` is the device's own MAC address, reported to the MacAddrDeviceInterface.
  // if `promisc_only` is true, the only filtering mode reported to the interface will be
  // `MODE_PROMISCUOUS`.
  // On success, the adapter is stored in `out`.
  static zx_status_t Create(MacAdapterParent* parent, fuchsia::net::MacAddress mac,
                            bool promisc_only, std::unique_ptr<MacAdapter>* out);
  // Binds the request channel to the MacAddrDeviceInterface. Requests will be served over the
  // provided `dispatcher`.
  zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel req);
  // Tears down this adapter and calls `callback` when teardown is finished.
  // Tearing down causes all client channels to be closed.
  // There are no guarantees over which thread `callback` is called.
  // It is invalid to attempt to tear down an adapter that is already tearing down or is already
  // torn down.
  void Teardown(fit::callback<void()> callback);
  // Same as `Teardown`, but blocks until teardown is complete.
  void TeardownSync();

  const fuchsia::net::MacAddress& mac() { return mac_; }

  // MacAddrImpl protocol:
  void MacAddrImplGetAddress(uint8_t out_mac[MAC_SIZE]);
  void MacAddrImplGetFeatures(features_t* out_features);
  void MacAddrImplSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                          size_t multicast_macs_count);

  // Clones the internal mac state into `out`.
  void CloneMacState(fuchsia::net::tun::MacState* out);

 private:
  MacAdapter(MacAdapterParent* parent, fuchsia::net::MacAddress mac, bool promisc_only)
      : parent_(parent), mac_(mac), promisc_only_(promisc_only) {}

  fbl::Mutex state_lock_;
  std::unique_ptr<MacAddrDeviceInterface> device_;
  MacAdapterParent* const parent_;  // pointer to parent, not owned.
  fuchsia::net::MacAddress mac_;
  const bool promisc_only_;
  fuchsia::net::tun::MacState mac_state_ __TA_GUARDED(state_lock_);
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_MAC_ADAPTER_H_
