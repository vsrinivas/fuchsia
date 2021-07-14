// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net_interfaces.h"

#include <netinet/in.h>

#include <string>
#include <unordered_map>
#include <variant>

#include "src/lib/fxl/strings/string_printf.h"

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

bool Properties::operator==(const Properties& rhs) const {
  return fidl::Equals(properties_, rhs.properties_);
}

bool Properties::is_loopback() const {
  return device_class().Which() == fuchsia::net::interfaces::DeviceClass::kLoopback;
}

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

bool Properties::IsGloballyRoutable() const {
  if (is_loopback() || !online()) {
    return false;
  }
  for (const auto& address : addresses()) {
    const auto& addr = address.addr().addr;
    switch (addr.Which()) {
      case fuchsia::net::IpAddress::Tag::kIpv4: {
        if (has_default_ipv4_route()) {
          return true;
        }
        break;
      }
      case fuchsia::net::IpAddress::Tag::kIpv6: {
        if (has_default_ipv6_route() && !IN6_IS_ADDR_LINKLOCAL(addr.ipv6().addr.data())) {
          return true;
        }
        break;
      }
      case fuchsia::net::IpAddress::Tag::Invalid: {
        break;
      }
    }
  }
  return false;
}

PropertiesMap::PropertiesMap() noexcept = default;

PropertiesMap::PropertiesMap(PropertiesMap&& interface) noexcept = default;

PropertiesMap& PropertiesMap::operator=(PropertiesMap&& interface) noexcept = default;

PropertiesMap::~PropertiesMap() = default;

// static
std::string PropertiesMap::update_error_get_string(UpdateErrorVariant variant) {
  UpdateErrorVisitor visitor{
      [](UpdateError err) {
        switch (err) {
          case UpdateError::kInvalidExisting:
            return std::string("invalid properties in Existing event");
          case UpdateError::kInvalidAdded:
            return std::string("invalid properties in Added event");
          case UpdateError::kMissingId:
            return std::string("missing interface ID in Changed event");
          case UpdateError::kInvalidChanged:
            return std::string("invalid Changed event");
          case UpdateError::kInvalidEvent:
            return std::string("invalid event type");
        }
      },
      [](UpdateErrorWithId<UpdateErrorWithIdKind::kDuplicateExisting> err) {
        return fxl::StringPrintf("duplicate interface (id=%lu) in Existing event", err.id);
      },
      [](UpdateErrorWithId<UpdateErrorWithIdKind::kDuplicateAdded> err) {
        return fxl::StringPrintf("duplicate interface (id=%lu) in Added event", err.id);
      },
      [](UpdateErrorWithId<UpdateErrorWithIdKind::kUnknownChanged> err) {
        return fxl::StringPrintf("unknown interface (id=%lu) in Changed event", err.id);
      },
      [](UpdateErrorWithId<UpdateErrorWithIdKind::kUnknownRemoved> err) {
        return fxl::StringPrintf("unknown interface (id=%lu) in Removed event", err.id);
      },
  };
  return std::visit(visitor, variant);
}

fpromise::result<void, PropertiesMap::UpdateErrorVariant> PropertiesMap::Update(
    fuchsia::net::interfaces::Event event) {
  switch (event.Which()) {
    case fuchsia::net::interfaces::Event::kExisting: {
      auto validated_properties = Properties::VerifyAndCreate(std::move(event.existing()));
      if (!validated_properties.has_value()) {
        return fpromise::error(
            PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError::kInvalidExisting));
      }

      const auto& [iter, inserted] =
          properties_map_.emplace(validated_properties->id(), std::move(*validated_properties));
      if (!inserted) {
        return fpromise::error(PropertiesMap::UpdateErrorVariant(
            PropertiesMap::UpdateErrorWithId<
                PropertiesMap::UpdateErrorWithIdKind::kDuplicateExisting>{
                .id = validated_properties->id()}));
      }
      return fpromise::ok();
    }
    case fuchsia::net::interfaces::Event::kAdded: {
      auto validated_properties = Properties::VerifyAndCreate(std::move(event.added()));
      if (!validated_properties.has_value()) {
        return fpromise::error(
            PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError::kInvalidAdded));
      }

      const auto& [iter, inserted] =
          properties_map_.emplace(validated_properties->id(), std::move(*validated_properties));
      if (!inserted) {
        return fpromise::error(PropertiesMap::UpdateErrorVariant(
            PropertiesMap::UpdateErrorWithId<PropertiesMap::UpdateErrorWithIdKind::kDuplicateAdded>{
                .id = validated_properties->id()}));
      }
      return fpromise::ok();
    }
    case fuchsia::net::interfaces::Event::kChanged: {
      fuchsia::net::interfaces::Properties& change = event.changed();
      if (!change.has_id()) {
        return fpromise::error(
            PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError::kMissingId));
      }
      auto it = properties_map_.find(change.id());
      if (it == properties_map_.end()) {
        return fpromise::error(PropertiesMap::UpdateErrorVariant(
            PropertiesMap::UpdateErrorWithId<PropertiesMap::UpdateErrorWithIdKind::kUnknownChanged>{
                .id = change.id()}));
      }

      auto& properties = it->second;
      if (!properties.Update(&change)) {
        return fpromise::error(
            PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError::kInvalidChanged));
      }
      return fpromise::ok();
    }
    case fuchsia::net::interfaces::Event::kRemoved: {
      const auto nh = properties_map_.extract(event.removed());
      if (nh.empty()) {
        return fpromise::error(PropertiesMap::UpdateErrorVariant(
            PropertiesMap::UpdateErrorWithId<PropertiesMap::UpdateErrorWithIdKind::kUnknownRemoved>{
                .id = event.removed()}));
      }
      return fpromise::ok();
    }
    case fuchsia::net::interfaces::Event::kIdle: {
      return fpromise::ok();
    }
    case fuchsia::net::interfaces::Event::Invalid: {
      return fpromise::error(
          PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError::kInvalidEvent));
    }
  }
}

}  // namespace net::interfaces
