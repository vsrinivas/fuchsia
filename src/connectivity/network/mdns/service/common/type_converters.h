// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPE_CONVERTERS_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPE_CONVERTERS_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/fidl/cpp/type_converter.h"
#include "src/connectivity/network/mdns/service/common/types.h"
#include "src/lib/fsl/types/type_converters.h"

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

}  // namespace fidl

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPE_CONVERTERS_H_
