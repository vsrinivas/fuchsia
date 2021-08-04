// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_common.h"

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/testing/predicates/status.h"

namespace net::interfaces::test {

namespace {

fuchsia::net::interfaces::Properties NewProperties(uint64_t id, bool reachable) {
  fuchsia::net::interfaces::Properties properties;
  properties.set_id(id);
  properties.set_name(kName);
  properties.set_device_class(fuchsia::net::interfaces::DeviceClass::WithDevice(
      fuchsia::hardware::network::DeviceClass::ETHERNET));
  SetMutableProperties(properties, reachable, true, true, Addresses(kIPv4Address1, kIPv6Address1));
  return properties;
}

}  // namespace

fuchsia::net::Ipv4Address ToFIDL(const std::array<uint8_t, 4>& bytes) {
  fuchsia::net::Ipv4Address addr;
  std::copy(bytes.cbegin(), bytes.cend(), addr.addr.begin());
  return addr;
}

fuchsia::net::Ipv6Address ToFIDL(const std::array<uint8_t, 16>& bytes) {
  fuchsia::net::Ipv6Address addr;
  std::copy(bytes.cbegin(), bytes.cend(), addr.addr.begin());
  return addr;
}

void InitAddress(fuchsia::net::interfaces::Address& addr, const std::array<uint8_t, 4>& bytes) {
  fuchsia::net::Subnet subnet{
      .prefix_len = kIPv4PrefixLength,
  };
  subnet.addr.set_ipv4(ToFIDL(bytes));
  addr.set_addr(std::move(subnet));
}

void InitAddress(fuchsia::net::interfaces::Address& addr, const std::array<uint8_t, 16>& bytes) {
  fuchsia::net::Subnet subnet{
      .prefix_len = kIPv6PrefixLength,
  };
  subnet.addr.set_ipv6(ToFIDL(bytes));
  addr.set_addr(std::move(subnet));
}

void AppendAddresses(std::vector<fuchsia::net::interfaces::Address>& addresses) {}

void SetMutableProperties(fuchsia::net::interfaces::Properties& properties, bool online,
                          bool has_default_ipv4_route, bool has_default_ipv6_route,
                          std::vector<fuchsia::net::interfaces::Address> addresses) {
  properties.set_online(online);
  properties.set_has_default_ipv4_route(has_default_ipv4_route);
  properties.set_has_default_ipv6_route(has_default_ipv6_route);
  properties.set_addresses(std::move(addresses));
}

void FakeWatcherImpl::SendAddedEvent(uint64_t id, bool reachable) {
  SendEvent(fuchsia::net::interfaces::Event::WithAdded(NewProperties(id, reachable)));
}

void FakeWatcherImpl::SendExistingEvent(uint64_t id, bool reachable) {
  SendEvent(fuchsia::net::interfaces::Event::WithExisting(NewProperties(id, reachable)));
}

void FakeWatcherImpl::SendChangedEvent(uint64_t id, bool reachable) {
  fuchsia::net::interfaces::Properties properties;
  properties.set_id(id);
  properties.set_online(reachable);

  SendEvent(fuchsia::net::interfaces::Event::WithChanged(std::move(properties)));
}

void FakeWatcherImpl::SendRemovedEvent(uint64_t id) {
  SendEvent(fuchsia::net::interfaces::Event::WithRemoved(std::move(id)));
}

void FakeWatcherImpl::NotImplemented_(const std::string& name) {
  FX_NOTIMPLEMENTED() << name << " is not implemented";
}

void FakeWatcherImpl::Watch(WatchCallback callback) {
  ASSERT_FALSE(watch_callback_.has_value());
  watch_callback_.emplace(std::move(callback));
}

void FakeWatcherImpl::SendEvent(fuchsia::net::interfaces::Event event) {
  ASSERT_TRUE(watch_callback_.has_value());
  (*watch_callback_)(std::move(event));
  watch_callback_.reset();
}

void FakeWatcherImpl::Reset() { watch_callback_.reset(); }

bool FakeWatcherImpl::IsWatchPending() { return watch_callback_.has_value(); }

}  // namespace net::interfaces::test
