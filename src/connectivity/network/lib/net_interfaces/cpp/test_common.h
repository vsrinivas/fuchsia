// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_TEST_COMMON_H_
#define SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_TEST_COMMON_H_

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fpromise/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>

#include <vector>

#include "lib/gtest/test_loop_fixture.h"
#include "reachability.h"

namespace net::interfaces::test {

inline constexpr std::array<uint8_t, 4> kIPv4Address1 = {1, 2, 3, 4};
inline constexpr std::array<uint8_t, 4> kIPv4Address2 = {5, 6, 7, 8};
inline constexpr std::array<uint8_t, 16> kIPv6Address1 = {0x01, 0x23, 0x45, 0x67, 0, 0, 0, 0,
                                                          0,    0,    0,    0,    0, 0, 0, 1};
inline constexpr std::array<uint8_t, 16> kIPv6Address2 = {0x89, 0xAB, 0xCD, 0xEF, 0, 0, 0, 0,
                                                          0,    0,    0,    0,    0, 0, 0, 1};
inline constexpr std::array<uint8_t, 16> kIPv6LLAddress = {0xfe, 0x80, 0x00, 0x00, 0, 0, 0, 0,
                                                           0,    0,    0,    0,    0, 0, 0, 1};
inline constexpr uint8_t kIPv4PrefixLength = 24;
inline constexpr uint8_t kIPv6PrefixLength = 64;

inline constexpr const char kName[] = "test01";

inline constexpr zx::duration kInitialBackoffDelay = zx::msec(100);

void InitAddress(fuchsia::net::interfaces::Address& addr, const std::array<uint8_t, 4>& bytes);
void InitAddress(fuchsia::net::interfaces::Address& addr, const std::array<uint8_t, 16>& bytes);

void AppendAddresses(std::vector<fuchsia::net::interfaces::Address>& addresses);

template <typename T, typename... Ts>
void AppendAddresses(std::vector<fuchsia::net::interfaces::Address>& addresses, T bytes,
                     Ts... byte_arrays) {
  InitAddress(addresses.emplace_back(), bytes);
  AppendAddresses(addresses, byte_arrays...);
}

// Creates a vector of addresses by initializing each address using |byte_arrays|.
// Note that each element in the parameter pack |byte_arrays| must have a type convertible to |const
// std::array<uint8_t, 4>&| or |const std::array<uint8_t, 16>&|.
//
// Thes function exists mostly to work around the fact that it's not possible to use an initializer
// list to initialize the return type of this function since |fuchsia::net::interfaces::Address| is
// move-only.
template <typename... Ts>
std::vector<fuchsia::net::interfaces::Address> Addresses(Ts... byte_arrays) {
  std::vector<fuchsia::net::interfaces::Address> addresses;
  addresses.reserve(sizeof...(Ts));
  AppendAddresses(addresses, byte_arrays...);
  return addresses;
}

void SetMutableProperties(fuchsia::net::interfaces::Properties& properties, bool online,
                          bool has_default_ipv4_route, bool has_default_ipv6_route,
                          std::vector<fuchsia::net::interfaces::Address> addresses);

class FakeWatcherImpl : public fuchsia::net::interfaces::testing::Watcher_TestBase {
 public:
  // |TestBase|
  void NotImplemented_(const std::string& name) override;

  void Watch(WatchCallback callback) override;

  void SendEvent(fuchsia::net::interfaces::Event event);

  void SendAddedEvent(uint64_t id, bool reachable);

  void SendExistingEvent(uint64_t id, bool reachable);

  void SendChangedEvent(uint64_t id, bool reachable);

  void SendRemovedEvent(uint64_t id);

  void Reset();

  bool IsWatchPending();

 private:
  std::optional<WatchCallback> watch_callback_;
};

}  // namespace net::interfaces::test

#endif  // SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_TEST_COMMON_H_
