// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_ADDR_H_
#define SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_ADDR_H_

#include <netinet/in.h>

#include <optional>
#include <string>

std::string Format(const sockaddr_storage& addr);

// Parse an address of the form \[IP[%ID]\]:PORT.
std::optional<sockaddr_storage> Parse(const std::string& ip_port_str);

// Parse an IP address and optional port.
std::optional<sockaddr_storage> Parse(const std::string& ip_str,
                                      const std::optional<std::string>& port_str);

// Parse an IPv4 address with optional index specifier of the form IP[%ID].
//
// This function exists because sockaddr_in does not carry an interface index.
std::pair<std::optional<in_addr>, std::optional<int>> ParseIpv4WithScope(
    const std::string& ip_id_str);

#endif  // SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_ADDR_H_
