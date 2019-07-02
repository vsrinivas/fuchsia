// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_OVERNET_FAKE_OVERNET_H_
#define SRC_LEDGER_BIN_TESTING_OVERNET_FAKE_OVERNET_H_

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/component/cpp/service_provider_impl.h>
#include <lib/fit/function.h>

#include "src/lib/fxl/macros.h"

namespace ledger {

// FakeOvernet implements Overnet. It acts as the singleton
// Overnet for a (virtual) host.
class FakeOvernet : public fuchsia::overnet::Overnet {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Returns the list of known devices. See Overnet::GetKnownDeviceNames
    // for more details.
    virtual void ListPeers(
        uint64_t last_version,
        fit::function<void(uint64_t, std::vector<fuchsia::overnet::protocol::NodeId>)>
            callback) = 0;

    // Connects to the ServiceProvider from host |device_name|.
    virtual void ConnectToService(fuchsia::overnet::protocol::NodeId device_name,
                                  std::string service_name, zx::channel channel) = 0;
  };

  explicit FakeOvernet(uint64_t self_id, Delegate* delegate);
  ~FakeOvernet() override {}

  // Connects to the service provider of this (virtual) host
  void GetService(std::string service_name, zx::channel chan);

 private:
  class ServiceProviderHolder {
   public:
    explicit ServiceProviderHolder(fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider>);

    void set_on_empty(fit::closure on_empty);

    fuchsia::overnet::ServiceProvider* operator->() const;
    fuchsia::overnet::ServiceProvider& operator*() const;

   private:
    fuchsia::overnet::ServiceProviderPtr ptr_;
    fit::closure on_empty_;
  };

  // Overnet implementation:
  void RegisterService(
      std::string name,
      fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> service_provider) override;
  void ConnectToService(fuchsia::overnet::protocol::NodeId node, std::string service_name,
                        zx::channel channel) override;
  void ListPeers(uint64_t version_last_seen, ListPeersCallback callback) override;

  uint64_t const self_id_;
  Delegate* const delegate_;
  callback::AutoCleanableMap<std::string, ServiceProviderHolder> service_providers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeOvernet);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_OVERNET_FAKE_OVERNET_H_
