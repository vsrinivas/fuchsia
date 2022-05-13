// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_NETWORK_REACHABILITY_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_NETWORK_REACHABILITY_PROVIDER_H_

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>

#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics::stubs {

constexpr uint64_t kID = 1;
constexpr const char kName[] = "test01";
constexpr std::array<uint8_t, 4> kIPv4Address = {1, 2, 3, 1};
constexpr std::array<uint8_t, 16> kIPv6Address = {0x01, 0x23, 0x45, 0x67, 0, 0, 0, 0,
                                                  0,    0,    0,    0,    0, 0, 0, 1};
constexpr uint8_t kIPv4PrefixLength = 24;
constexpr uint8_t kIPv6PrefixLength = 64;

class NetworkReachabilityProvider
    : public SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::net::interfaces, State) {
 public:
  NetworkReachabilityProvider();

  void GetWatcher(fuchsia::net::interfaces::WatcherOptions options,
                  ::fidl::InterfaceRequest<fuchsia::net::interfaces::Watcher> watcher) override;

  void TriggerOnNetworkReachable(bool reachable);

 private:
  class FakeWatcherImpl : public fuchsia::net::interfaces::testing::Watcher_TestBase {
   public:
    void NotImplemented_(const std::string& name) override;

    void Watch(WatchCallback callback) override;

    void TriggerOnNetworkReachable(bool reachable);

    void Reset();

   private:
    fuchsia::net::interfaces::Event ExistingEvent(bool reachable);
    fuchsia::net::interfaces::Event ChangedEvent(bool reachable);

    std::optional<WatchCallback> watch_callback_;
    std::optional<bool> reachability_;
  };

  FakeWatcherImpl fake_watcher_impl_;
  const std::unique_ptr<::fidl::Binding<fuchsia::net::interfaces::Watcher>> binding_;
};

}  // namespace forensics::stubs

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_NETWORK_REACHABILITY_PROVIDER_H_
