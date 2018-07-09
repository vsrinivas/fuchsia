// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_DNS_FORMATTING_H_
#define GARNET_BIN_MDNS_SERVICE_DNS_FORMATTING_H_

#include <iomanip>
#include <iostream>
#include <memory>

#include "garnet/bin/mdns/service/dns_message.h"
#include "lib/fostr/indent.h"

namespace mdns {

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& value) {
  if (value.size() == 0) {
    return os << "<empty>\n";
  }

  int index = 0;
  for (const T& element : value) {
    os << fostr::NewLine << "[" << index++ << "] " << element;
  }

  return os;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::shared_ptr<T>& value) {
  return os << *value;
}

std::ostream& operator<<(std::ostream& os, DnsType value);
std::ostream& operator<<(std::ostream& os, DnsClass value);
std::ostream& operator<<(std::ostream& os, const DnsName& value);
std::ostream& operator<<(std::ostream& os, const DnsV4Address& value);
std::ostream& operator<<(std::ostream& os, const DnsV6Address& value);
std::ostream& operator<<(std::ostream& os, const DnsHeader& value);
std::ostream& operator<<(std::ostream& os, const DnsQuestion& value);
std::ostream& operator<<(std::ostream& os, const DnsResource& value);
std::ostream& operator<<(std::ostream& os, const DnsMessage& value);

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_DNS_FORMATTING_H_
