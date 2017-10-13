// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/netconnector/socket_address.h"

namespace netconnector {
namespace mdns {

struct MdnsNames {
  // Constructs a local host name from a simple host name. For example, produces
  // "host.local." from "host". The simple host name must not end in a ".".
  static std::string LocalHostFullName(const std::string& host_name);

  // Constructs a local service name from a simple service name. For example,
  // produces "_foo._tcp.local." from "_foo._tcp.". The simple service name
  // must end in ".".
  static std::string LocalServiceFullName(const std::string& service_name);

  // Constructs a local service name from a simple service name and subtype.
  // For example, produces "_bar._sub_foo._tcp.local." from "_foo._tcp." and
  // subtype "_bar.". The simple service name and subtype must both end in ".".
  static std::string LocalServiceSubtypeFullName(
      const std::string& service_name,
      const std::string& subtype);

  // Constructs a local service instance name from a simple instance name and
  // a simple service name. For example, produces "myfoo._foo._tcp.local." from
  // "myfoo" and "_foo._tcp.". The simple instance name must not end in a ".",
  // and the simple service name must end in ".".
  static std::string LocalInstanceFullName(const std::string& instance_name,
                                           const std::string& service_name);

  // Constructs a local service subtype instance name from a simple instance
  // name, a simple service name and a subtype. For example, produces
  // "myfoo._bar._sub._foo._tcp.local." from instance name "myfoo", service name
  // "_foo._tcp." and subtype "_bar". The simple instance name and subtype must
  // not end in a ".", and the simple service name must end in ".".
  static std::string LocalInstanceSubtypeFullName(
      const std::string& instance_name,
      const std::string& service_name,
      const std::string& subtype);

  // Extracts the simple instance name from an instance full name given the
  // name of the service. Returns true and deposits the instance name if
  // successful, return false if not.
  static bool ExtractInstanceName(const std::string& instance_full_name,
                                  const std::string& service_name,
                                  std::string* instance_name);

  // Determines if |name| is a local service name matching |service_name| or
  // a subtype of |service_name|. If |name| does specify a subtype, the
  // subtype is returned via |subtype_out|. Otherwise |*subtype_out| is
  // cleared.
  static bool MatchServiceName(const std::string& name,
                               const std::string& service_name,
                               std::string* subtype_out);

  // Determines if |name| is a local instance name matching |instance_name| and
  // |service_name| or a subtype of |service_name|. If |name| does specify a
  // subtype, the subtype is returned via |subtype_out|. Otherwise
  // |*subtype_out| is cleared.
  static bool MatchInstanceName(const std::string& name,
                                const std::string& instance_name,
                                const std::string& service_name,
                                std::string* subtype_out);

  // Determines if |host_name| is a valid host name.
  static bool IsValidHostName(const std::string& host_name);

  // Determines if |service_name| is a valid simple service name.
  static bool IsValidServiceName(const std::string& service_name);

  // Determines if |instance_name| is a valid simple instance name.
  static bool IsValidInstanceName(const std::string& instance_name);
};

}  // namespace mdns
}  // namespace netconnector
