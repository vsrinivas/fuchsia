// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_OVERNET_FAKE_OVERNET_H_
#define SRC_LEDGER_BIN_TESTING_OVERNET_FAKE_OVERNET_H_

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/fit/function.h>

#include "src/lib/callback/auto_cleanable.h"

namespace ledger {

// FakeOvernet implements Overnet. It acts as the singleton
// Overnet for a (virtual) host.
class FakeOvernet : public fuchsia::overnet::Overnet {
 public:
  class Delegate {
   public:
    // Holds the information necessary to create an overnet::Peer.
    struct FakePeer {
      fuchsia::overnet::protocol::NodeId id;
      std::vector<std::string> services;
    };

    virtual ~Delegate() = default;

    // Returns the list of known devices. See Overnet::GetKnownDeviceNames
    // for more details.
    virtual void ListPeers(uint64_t last_version,
                           fit::function<void(uint64_t, std::vector<FakePeer>)> callback) = 0;

    // Connects to the ServiceProvider from host |device_name|.
    virtual void ConnectToService(fuchsia::overnet::protocol::NodeId device_name,
                                  std::string service_name, zx::channel channel) = 0;

    // Called when a service was registered to this Overnet.
    virtual void ServiceWasRegistered() = 0;
  };

  explicit FakeOvernet(async_dispatcher_t* dispatcher, uint64_t self_id, Delegate* delegate);
  FakeOvernet(const FakeOvernet&) = delete;
  FakeOvernet& operator=(const FakeOvernet&) = delete;
  ~FakeOvernet() override = default;

  // Connects to the service provider of this (virtual) host
  void GetService(std::string service_name, zx::channel chan);

  // Returns the list of services registered to this Overnet.
  std::vector<std::string> GetAllServices() const;

 private:
  class ServiceProviderHolder {
   public:
    explicit ServiceProviderHolder(fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider>);

    void SetOnDiscardable(fit::closure on_discardable);
    bool IsDiscardable() const;

    fuchsia::overnet::ServiceProvider* operator->() const;
    fuchsia::overnet::ServiceProvider& operator*() const;

   private:
    fuchsia::overnet::ServiceProviderPtr ptr_;
    fit::closure on_discardable_;
  };

  // Overnet implementation:
  void PublishService(
      std::string name,
      fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> service_provider) override;
  void ConnectToService(fuchsia::overnet::protocol::NodeId node, std::string service_name,
                        zx::channel channel) override;
  void ListPeers(ListPeersCallback callback) override;

  uint64_t const self_id_;
  Delegate* const delegate_;
  callback::AutoCleanableMap<std::string, ServiceProviderHolder> service_providers_;
  std::shared_ptr<uint64_t> last_seen_peers_version_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_OVERNET_FAKE_OVERNET_H_
