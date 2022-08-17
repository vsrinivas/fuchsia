// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPE_CONVERTERS_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPE_CONVERTERS_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/fidl/cpp/type_converter.h"
#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/service_instance.h"
#include "src/connectivity/network/mdns/service/common/types.h"
#include "src/connectivity/network/mdns/service/encoding/dns_message.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/inet/socket_address.h"

namespace fidl {

template <>
struct TypeConverter<mdns::Media, fuchsia::net::mdns::Media> {
  static mdns::Media Convert(fuchsia::net::mdns::Media value) {
    switch (value) {
      case fuchsia::net::mdns::Media::WIRED:
        return mdns::Media::kWired;
      case fuchsia::net::mdns::Media::WIRELESS:
        return mdns::Media::kWireless;
      default:
        FX_DCHECK(value == (fuchsia::net::mdns::Media::WIRED | fuchsia::net::mdns::Media::WIRELESS))
            << "Unrecognized fuchsia::net::mdns::Media value " << static_cast<uint32_t>(value);
        return mdns::Media::kBoth;
    }
  }
};

template <>
struct TypeConverter<mdns::IpVersions, fuchsia::net::mdns::IpVersions> {
  static mdns::IpVersions Convert(fuchsia::net::mdns::IpVersions value) {
    switch (value) {
      case fuchsia::net::mdns::IpVersions::V4:
        return mdns::IpVersions::kV4;
      case fuchsia::net::mdns::IpVersions::V6:
        return mdns::IpVersions::kV6;
      default:
        FX_DCHECK(value ==
                  (fuchsia::net::mdns::IpVersions::V4 | fuchsia::net::mdns::IpVersions::V6));
        return mdns::IpVersions::kBoth;
    }
  }
};

template <>
struct TypeConverter<fuchsia::net::mdns::ServiceInstancePublicationCause, mdns::PublicationCause> {
  static fuchsia::net::mdns::ServiceInstancePublicationCause Convert(mdns::PublicationCause value) {
    switch (value) {
      case mdns::PublicationCause::kAnnouncement:
        return fuchsia::net::mdns::ServiceInstancePublicationCause::ANNOUNCEMENT;
      case mdns::PublicationCause::kQueryMulticastResponse:
        return fuchsia::net::mdns::ServiceInstancePublicationCause::QUERY_MULTICAST_RESPONSE;
      case mdns::PublicationCause::kQueryUnicastResponse:
        return fuchsia::net::mdns::ServiceInstancePublicationCause::QUERY_UNICAST_RESPONSE;
    }
  }
};

template <>
struct TypeConverter<fuchsia::net::mdns::PublicationCause, mdns::PublicationCause> {
  static fuchsia::net::mdns::PublicationCause Convert(mdns::PublicationCause value) {
    switch (value) {
      case mdns::PublicationCause::kAnnouncement:
        return fuchsia::net::mdns::PublicationCause::ANNOUNCEMENT;
      case mdns::PublicationCause::kQueryMulticastResponse:
        return fuchsia::net::mdns::PublicationCause::QUERY_MULTICAST_RESPONSE;
      case mdns::PublicationCause::kQueryUnicastResponse:
        return fuchsia::net::mdns::PublicationCause::QUERY_UNICAST_RESPONSE;
    }
  }
};

template <>
struct TypeConverter<std::string, std::vector<uint8_t>> {
  static std::string Convert(const std::vector<uint8_t>& value) {
    return std::string(value.begin(), value.end());
  }
};

template <>
struct TypeConverter<std::vector<uint8_t>, std::string> {
  static std::vector<uint8_t> Convert(const std::string& value) {
    return std::vector<uint8_t>(value.data(), value.data() + value.size());
  }
};

template <>
struct TypeConverter<fuchsia::net::mdns::HostAddress, mdns::HostAddress> {
  static fuchsia::net::mdns::HostAddress Convert(const mdns::HostAddress& value) {
    return fuchsia::net::mdns::HostAddress{
        .address = static_cast<fuchsia::net::IpAddress>(value.address()),
        .interface = value.interface_id(),
        .ttl = value.ttl().get()};
  }
};

template <>
struct TypeConverter<mdns::HostAddress, inet::IpAddress> {
  static mdns::HostAddress Convert(const inet::IpAddress& value) {
    return mdns::HostAddress(value, 0, zx::sec(mdns::DnsResource::kShortTimeToLive));
  }
};

template <>
struct TypeConverter<fuchsia::net::SocketAddress, inet::SocketAddress> {
  static fuchsia::net::SocketAddress Convert(const inet::SocketAddress& value) {
    return static_cast<fuchsia::net::SocketAddress>(value);
  }
};

template <>
struct TypeConverter<std::unique_ptr<mdns::Mdns::Publication>, fuchsia::net::mdns::PublicationPtr> {
  static std::unique_ptr<mdns::Mdns::Publication> Convert(
      const fuchsia::net::mdns::PublicationPtr& value) {
    if (!value) {
      return nullptr;
    }

    auto publication = mdns::Mdns::Publication::Create(
        inet::IpPort::From_uint16_t(value->port),
        fidl::To<std::vector<std::vector<std::uint8_t>>>(value->text), value->srv_priority,
        value->srv_weight);

    auto ensure_uint32_secs = [](int64_t nanos) -> uint32_t {
      const int64_t secs = zx::nsec(nanos).to_secs();
      FX_CHECK(secs >= 0 && secs < std::numeric_limits<uint32_t>::max())
          << secs << " doesn't fit a uint32";
      return static_cast<uint32_t>(secs);
    };

    publication->ptr_ttl_seconds_ = ensure_uint32_secs(value->ptr_ttl);
    publication->srv_ttl_seconds_ = ensure_uint32_secs(value->srv_ttl);
    publication->txt_ttl_seconds_ = ensure_uint32_secs(value->txt_ttl);

    return publication;
  }
};

template <>
struct TypeConverter<std::unique_ptr<mdns::Mdns::Publication>,
                     fuchsia::net::mdns::ServiceInstancePublication> {
  static std::unique_ptr<mdns::Mdns::Publication> Convert(
      const fuchsia::net::mdns::ServiceInstancePublication& value) {
    constexpr uint16_t kDefaultSrvPriority = 0;
    constexpr uint16_t kDefaultSrvWeight = 0;

    if (!value.has_port()) {
      FX_LOGS(ERROR) << "ServiceInstancePublication has no port value, closing connection.";
      return nullptr;
    }

    std::vector<std::vector<uint8_t>> text;
    if (value.has_text()) {
      text = value.text();
    }

    uint16_t srv_priority = value.has_srv_priority() ? value.srv_priority() : kDefaultSrvPriority;
    uint16_t srv_weight = value.has_srv_weight() ? value.srv_weight() : kDefaultSrvWeight;

    auto result = mdns::Mdns::Publication::Create(inet::IpPort::From_uint16_t(value.port()), text,
                                                  srv_priority, srv_weight);

    if (value.has_ptr_ttl()) {
      const int64_t secs = zx::nsec(value.ptr_ttl()).to_secs();
      if (secs < 0 || secs > std::numeric_limits<uint32_t>::max()) {
        FX_LOGS(ERROR) << "ServiceInstancePublication has ptr_ttl value out of range, "
                          "closing connection.";
        return nullptr;
      }

      result->ptr_ttl_seconds_ = static_cast<uint32_t>(secs);
    }

    if (value.has_srv_ttl()) {
      const int64_t secs = zx::nsec(value.srv_ttl()).to_secs();
      if (secs < 0 || secs > std::numeric_limits<uint32_t>::max()) {
        FX_LOGS(ERROR) << "ServiceInstancePublication has srv_ttl value out of range, "
                          "closing connection.";
        return nullptr;
      }

      result->srv_ttl_seconds_ = static_cast<uint32_t>(secs);
    }

    if (value.has_txt_ttl()) {
      const int64_t secs = zx::nsec(value.txt_ttl()).to_secs();
      if (secs < 0 || secs > std::numeric_limits<uint32_t>::max()) {
        FX_LOGS(ERROR) << "ServiceInstancePublication has txt_ttl value out of range, "
                          "closing connection.";
        return nullptr;
      }

      result->txt_ttl_seconds_ = static_cast<uint32_t>(secs);
    }

    return result;
  }
};

template <>
struct TypeConverter<fuchsia::net::IpAddress, inet::SocketAddress> {
  static fuchsia::net::IpAddress Convert(const inet::SocketAddress& value) {
    return static_cast<fuchsia::net::IpAddress>(value.address());
  }
};

template <>
struct TypeConverter<fuchsia::net::mdns::ResourceType, mdns::DnsType> {
  static fuchsia::net::mdns::ResourceType Convert(const mdns::DnsType& value) {
    switch (value) {
      case mdns::DnsType::kPtr:
        return fuchsia::net::mdns::ResourceType::PTR;
      case mdns::DnsType::kAny:
        return fuchsia::net::mdns::ResourceType::ANY;
      default:
        FX_DCHECK(false) << "Asked to convert unexpected mdns::DnsType "
                         << static_cast<uint32_t>(value);
        return fuchsia::net::mdns::ResourceType::ANY;
    }
  }
};

template <typename T, typename U>
struct TypeConverter<std::vector<T>, std::vector<U>> {
  static std::vector<T> Convert(const std::vector<U>& value) {
    std::vector<T> result;
    std::transform(value.begin(), value.end(), std::back_inserter(result),
                   [](const U& u) { return fidl::To<T>(u); });
    return result;
  }
};

template <>
struct TypeConverter<fuchsia::net::mdns::ServiceInstance, mdns::ServiceInstance> {
  static fuchsia::net::mdns::ServiceInstance Convert(const mdns::ServiceInstance& value) {
    fuchsia::net::mdns::ServiceInstance result;
    result.set_service(value.service_name_);
    result.set_instance(value.instance_name_);
    result.set_srv_priority(value.srv_priority_);
    result.set_srv_weight(value.srv_weight_);
    result.set_target(value.target_name_);
    result.set_addresses(fidl::To<std::vector<fuchsia::net::SocketAddress>>(value.addresses_));
    result.set_text_strings(value.text_);

    // Deprecated items
    result.set_text(fidl::To<std::vector<std::string>>(value.text_));

    for (auto& address : result.addresses()) {
      if (address.is_ipv4()) {
        if (!result.has_ipv4_endpoint()) {
          result.set_ipv4_endpoint(fidl::Clone(address.ipv4()));
          if (result.has_ipv6_endpoint()) {
            break;
          }
        }
      } else {
        if (!result.has_ipv6_endpoint()) {
          result.set_ipv6_endpoint(fidl::Clone(address.ipv6()));
          if (result.has_ipv4_endpoint()) {
            break;
          }
        }
      }
    }

    return result;
  }
};

}  // namespace fidl

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPE_CONVERTERS_H_
