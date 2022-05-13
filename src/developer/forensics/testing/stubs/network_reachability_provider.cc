// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_reachability_provider.h"

#include <fuchsia/net/interfaces/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

namespace forensics::stubs {

NetworkReachabilityProvider::NetworkReachabilityProvider()
    : binding_(
          std::make_unique<fidl::Binding<fuchsia::net::interfaces::Watcher>>(&fake_watcher_impl_)) {
}

void NetworkReachabilityProvider::GetWatcher(
    fuchsia::net::interfaces::WatcherOptions options,
    fidl::InterfaceRequest<fuchsia::net::interfaces::Watcher> watcher) {
  ASSERT_FALSE(binding_ && binding_->is_bound());
  fake_watcher_impl_.Reset();
  ASSERT_OK(binding_->Bind(std::move(watcher)));
}

void NetworkReachabilityProvider::TriggerOnNetworkReachable(bool reachable) {
  fake_watcher_impl_.TriggerOnNetworkReachable(reachable);
}

void NetworkReachabilityProvider::FakeWatcherImpl::NotImplemented_(const std::string& name) {
  FX_NOTIMPLEMENTED() << name << " is not implemented";
}

void NetworkReachabilityProvider::FakeWatcherImpl::Watch(WatchCallback callback) {
  ASSERT_FALSE(watch_callback_.has_value());
  watch_callback_.emplace(std::move(callback));
}

void NetworkReachabilityProvider::FakeWatcherImpl::TriggerOnNetworkReachable(bool reachable) {
  ASSERT_FALSE(reachability_.has_value() && *reachability_ == reachable);
  ASSERT_TRUE(watch_callback_.has_value());

  if (!reachability_.has_value()) {
    (*watch_callback_)(ExistingEvent(reachable));
  } else {
    (*watch_callback_)(ChangedEvent(reachable));
  }

  watch_callback_.reset();

  reachability_ = reachable;
}

void NetworkReachabilityProvider::FakeWatcherImpl::Reset() { watch_callback_.reset(); }

fuchsia::net::interfaces::Event NetworkReachabilityProvider::FakeWatcherImpl::ExistingEvent(
    bool reachable) {
  fuchsia::net::interfaces::Event event;

  auto& properties = event.existing();
  properties.set_id(kID);
  properties.set_name(kName);
  properties.set_device_class(fuchsia::net::interfaces::DeviceClass::WithDevice(
      fuchsia::hardware::network::DeviceClass::WLAN));
  properties.set_has_default_ipv4_route(true);
  properties.set_has_default_ipv6_route(true);
  properties.set_online(reachable);

  properties.mutable_addresses()->reserve(2);
  auto& v4_interfaces_addr = properties.mutable_addresses()->emplace_back();
  v4_interfaces_addr.set_addr(fuchsia::net::Subnet{
      .addr = fuchsia::net::IpAddress::WithIpv4(fuchsia::net::Ipv4Address{
          .addr = kIPv4Address,
      }),
      .prefix_len = kIPv4PrefixLength,
  });
  auto& v6_interfaces_addr = properties.mutable_addresses()->emplace_back();
  v6_interfaces_addr.set_addr(fuchsia::net::Subnet{
      .addr = fuchsia::net::IpAddress::WithIpv6(fuchsia::net::Ipv6Address{
          .addr = kIPv6Address,
      }),
      .prefix_len = kIPv6PrefixLength,
  });

  return event;
}

fuchsia::net::interfaces::Event NetworkReachabilityProvider::FakeWatcherImpl::ChangedEvent(
    bool reachable) {
  fuchsia::net::interfaces::Event event;

  auto& properties = event.changed();
  properties.set_id(kID);
  properties.set_online(reachable);

  return event;
}

}  // namespace forensics::stubs
