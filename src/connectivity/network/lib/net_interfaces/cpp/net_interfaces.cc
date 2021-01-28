// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/lib/net_interfaces/cpp/net_interfaces.h"

namespace net::interfaces {

namespace {

bool VerifyCompleteProperties(const fuchsia::net::interfaces::Properties& properties) {
  if (!(properties.has_id() && properties.has_name() && properties.has_addresses() &&
        properties.has_online() && properties.has_device_class() &&
        properties.has_has_default_ipv4_route() && properties.has_has_default_ipv6_route())) {
    return false;
  }
  const auto& addresses = properties.addresses();
  return std::all_of(addresses.cbegin(), addresses.cend(),
                     [](const auto& address) { return address.has_addr(); });
}

}  // namespace

std::optional<Properties> Properties::VerifyAndCreate(
    fuchsia::net::interfaces::Properties properties) {
  if (!VerifyCompleteProperties(properties)) {
    return std::nullopt;
  }
  return std::make_optional(Properties(std::move(properties)));
}

Properties::Properties(fuchsia::net::interfaces::Properties properties)
    : properties_(std::move(properties)) {}

Properties::Properties(Properties&& interface) noexcept = default;

Properties& Properties::operator=(Properties&& interface) noexcept = default;

Properties::~Properties() = default;

bool Properties::Update(fuchsia::net::interfaces::Properties* properties) {
  if (!properties->has_id() || properties_.id() != properties->id()) {
    return false;
  }

  if (properties->has_addresses()) {
    const auto& addresses = properties->addresses();
    if (!std::all_of(addresses.cbegin(), addresses.cend(),
                     [](const auto& address) { return address.has_addr(); })) {
      return false;
    }
    if (!fidl::Equals(properties->addresses(), properties_.addresses())) {
      std::swap(*properties_.mutable_addresses(), *properties->mutable_addresses());
    } else {
      properties->clear_addresses();
    }
  }

  if (properties->has_online()) {
    if (properties->online() != properties_.online()) {
      std::swap(*properties_.mutable_online(), *properties->mutable_online());
    } else {
      properties->clear_online();
    }
  }
  if (properties->has_has_default_ipv4_route()) {
    if (properties->has_default_ipv4_route() != properties_.has_default_ipv4_route()) {
      std::swap(*properties_.mutable_has_default_ipv4_route(),
                *properties->mutable_has_default_ipv4_route());
    } else {
      properties->clear_has_default_ipv4_route();
    }
  }
  if (properties->has_has_default_ipv6_route()) {
    if (properties->has_default_ipv6_route() != properties_.has_default_ipv6_route()) {
      std::swap(*properties_.mutable_has_default_ipv6_route(),
                *properties->mutable_has_default_ipv6_route());
    } else {
      properties->clear_has_default_ipv6_route();
    }
  }

  properties->clear_id();
  return true;
}

}  // namespace net::interfaces
