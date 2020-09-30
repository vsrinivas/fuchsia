// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_MAC_INTERFACE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_MAC_INTERFACE_H_

#include <fuchsia/hardware/network/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/server.h>

#include <unordered_set>

#include <ddktl/protocol/network/mac.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "public/network_mac.h"

namespace network {

constexpr mode_t kSupportedModesMask =
    MODE_PROMISCUOUS | MODE_MULTICAST_PROMISCUOUS | MODE_MULTICAST_FILTER;
using MacAddress = llcpp::fuchsia::net::MacAddress;

namespace netdev = llcpp::fuchsia::hardware::network;

namespace internal {

class MacClientInstance;

// Implements the API translation between MacAddrImpl (banjo) and
// fuchsia.hardware.network.MacAddressing (FIDL).
class MacInterface : public ::network::MacAddrDeviceInterface {
 public:
  static zx_status_t Create(ddk::MacAddrImplProtocolClient parent,
                            std::unique_ptr<MacInterface>* out);

  ~MacInterface() override;

  // MacAddrDevice implementation:
  zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel req) override;
  void Teardown(fit::callback<void()> callback) override;

  // Converts a fuchsia.hardware.network.MacFilterMode to a valid mode to be communicated to the
  // device implementation, taking into consideration the device's available operating modes.
  mode_t ConvertMode(const netdev::MacFilterMode& mode);

 protected:
  friend MacClientInstance;
  // Consolidates all the requested operating modes and multicast filtering from all the attached
  // clients into a final operating mode and sets it on the parent device implementation.
  void Consolidate() __TA_REQUIRES(lock_);
  // Closes a client instance, causing a new operating mode to be calculated once the instance state
  // is removed. If the `MacInterface` is undergoing a teardown, the teardown will be finished if
  // there are no more open client instances.
  void CloseClient(MacClientInstance* client) __TA_EXCLUDES(lock_);

 private:
  explicit MacInterface(ddk::MacAddrImplProtocolClient parent);

  const ddk::MacAddrImplProtocolClient impl_;
  features_t features_{};
  mode_t default_mode_{};
  fbl::Mutex lock_;
  fbl::DoublyLinkedList<std::unique_ptr<MacClientInstance>> clients_ __TA_GUARDED(lock_);
  fit::callback<void()> teardown_callback_ __TA_GUARDED(lock_);
};

// The state associated with a FIDL client for fuchsia.hardware.network.MacAddressing.
class ClientState {
 public:
  // Helper class to store a Mac Address in an unoredered_set. Hashing is performed by the
  // `MacHasher` class.
  struct Addr {
    MacAddress address;
    bool operator==(const Addr& a) const {
      return std::equal(address.octets.begin(), address.octets.end(), a.address.octets.begin(),
                        a.address.octets.end());
    }
  };
  // Helper class to hash a Mac Address to store it in an unordered_set.
  class MacHasher {
   public:
    size_t operator()(const Addr& a) const {
      size_t hash = 0;
      size_t shift = 0;
      static_assert(decltype(a.address.octets)::size() <= sizeof(hash));
      for (uint8_t octet : a.address.octets) {
        hash |= static_cast<size_t>(octet) << shift;
        shift += 8;
      }
      return hash;
    }
  };

  explicit ClientState(mode_t filter_mode) : filter_mode(filter_mode) {}

  mode_t filter_mode;
  std::unordered_set<Addr, MacHasher> addresses;
};

// An instance representing a FIDL client to the MacAddressing server.
//
// `MacClientInstance` keeps the state associated with the client and is responsible for fulfilling
// FIDL requests.
class MacClientInstance : public netdev::MacAddressing::Interface,
                          public fbl::DoublyLinkedListable<std::unique_ptr<MacClientInstance>> {
 public:
  explicit MacClientInstance(MacInterface* parent, mode_t default_mode);

  void GetUnicastAddress(GetUnicastAddressCompleter::Sync& _completer) override;
  void SetMode(netdev::MacFilterMode mode, SetModeCompleter::Sync& _completer) override;
  void AddMulticastAddress(MacAddress address,
                           AddMulticastAddressCompleter::Sync& _completer) override;
  void RemoveMulticastAddress(MacAddress address,
                              RemoveMulticastAddressCompleter::Sync& _completer) override;
  // Binds the client instance to serve FIDL requests from the provided request channel.
  // All requests will be operated on the provided dispatcher.
  zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel req);
  // Unbinds the client instance.
  // Once unbound it'll call `CloseClient` on its parent asynchronously.
  void Unbind();

  // Accesses the client state.
  // Client state must only be accessed under the parent's MacInterface lock.
  const ClientState& state() const { return state_; }

 private:
  // Pointer to parent MacInterface, not owned.
  MacInterface* const parent_;
  ClientState state_ __TA_GUARDED(parent_->lock_);
  fit::optional<fidl::ServerBindingRef<netdev::MacAddressing>> binding_;
};

}  // namespace internal
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_MAC_INTERFACE_H_
