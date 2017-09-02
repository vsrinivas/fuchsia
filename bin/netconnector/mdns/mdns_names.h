// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "apps/netconnector/src/socket_address.h"

namespace netconnector {
namespace mdns {

struct MdnsNames {
  static const std::string kLocalDomainName;

  // Constructs a local host name from a simple host name. For example, produces
  // "host.local." from "host". The simple host name must not end in a ".".
  static std::string LocalHostFullName(const std::string& host_name);

  // Constructs a local service name from a simple service name. For example,
  // produces "_foo._tcp.local." from "_foo._tcp.". The simple service name
  // must end in ".".
  static std::string LocalServiceFullName(const std::string& service_name);

  // Constructs a local service instance name from a simple instance name and
  // a simple service name. For example, produces "myfoo._foo._tcp.local." from
  // "myfoo" and "_foo._tcp.local.". The simple instance name must not end in a
  // ".". The simple service name must end in ".".
  static std::string LocalInstanceFullName(const std::string& instance_name,
                                           const std::string& service_name);

  // Extracts the simple instance name from an instance full name given the
  // name of the service. Return true and deposits the instance name if
  // successful, return false if not.
  static bool ExtractInstanceName(const std::string& instance_full_name,
                                  const std::string& service_name,
                                  std::string* instance_name);

  // Determines if |service\ is a valid simple service name.
  static bool IsValidServiceName(const std::string& service_name);
};

}  // namespace mdns
}  // namespace netconnector
