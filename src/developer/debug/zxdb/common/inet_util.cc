// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/inet_util.h"

#include "src/lib/fxl/strings/string_number_conversions.h"

namespace zxdb {

Err ParseHostPort(const std::string& in_host, const std::string& in_port, std::string* out_host,
                  uint16_t* out_port) {
  if (in_host.empty()) {
    return Err(ErrType::kInput, "No host component specified.");
  }
  if (in_port.empty()) {
    return Err(ErrType::kInput, "No port component specified.");
  }

  // Trim brackets from the host name for IPv6 addresses.
  if (in_host.front() == '[' && in_host.back() == ']')
    *out_host = in_host.substr(1, in_host.size() - 2);
  else
    *out_host = in_host;

  uint16_t port;
  if (!fxl::StringToNumberWithError(in_port, &port)) {
    return Err(ErrType::kInput, "Invalid port number.");
  }
  if (port == 0) {
    return Err(ErrType::kInput, "Port value out of range.");
  }
  *out_port = port;

  return Err();
}

Err ParseHostPort(const std::string& input, std::string* out_host, uint16_t* out_port) {
  // Separate based on the last colon.
  size_t colon = input.rfind(':');
  if (colon == std::string::npos) {
    return Err(ErrType::kInput, "Expected colon to separate host/port.");
  }

  // If the host has a colon in it, it could be an IPv6 address. In this case,
  // require brackets around it to differentiate the case where people
  // supplied an IPv6 address and we just picked out the last component above.
  std::string host = input.substr(0, colon);
  if (host.empty()) {
    return Err(ErrType::kInput, "No host component specified.");
  }
  if (host.find(':') != std::string::npos) {
    if (host.front() != '[' || host.back() != ']') {
      return Err(ErrType::kInput, "Missing brackets enclosing IPv6 address, e.g., \"[::1]:1234\".");
    }
  }

  std::string port = input.substr(colon + 1);

  return ParseHostPort(host, port, out_host, out_port);
}

bool Ipv6HostPortIsMissingBrackets(const std::string& input) {
  size_t colon = input.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  std::string host = input.substr(0, colon);
  if (host.empty()) {
    return false;
  }
  if (host.find(':') == std::string::npos) {
    return false;
  }
  return host.front() != '[' || host.back() != ']';
}

}  // namespace zxdb
