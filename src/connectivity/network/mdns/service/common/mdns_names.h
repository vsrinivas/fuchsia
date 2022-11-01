// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_MDNS_NAMES_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_MDNS_NAMES_H_

#include <string>

#include "src/lib/inet/socket_address.h"

namespace mdns {

struct MdnsNames {
  // Name used to query for any service on the subnet.
  static const std::string kAnyServiceFullName;

  // Constructs a local host name from a simple host name. For example, produces
  // "host.local." from "host".
  static std::string HostFullName(const std::string& host_name);

  // Constructs a simple host name from a local host name. For example, produces
  // "host" from "host.local.".
  static std::string HostNameFromFullName(const std::string& host_full_name);

  // Constructs a local service name from a simple service name. For example,
  // produces "_foo._tcp.local." from "_foo._tcp.".
  static std::string ServiceFullName(const std::string& service_name);

  // Constructs a local service name from a simple service name and subtype.
  // For example, produces "_bar._sub_.foo._tcp.local." from "_foo._tcp." and
  // subtype "_bar".
  static std::string ServiceSubtypeFullName(const std::string& service_name,
                                            const std::string& subtype);

  // Constructs a local service instance name from a simple instance name and
  // a simple service name. For example, produces "myfoo._foo._tcp.local." from
  // "myfoo" and "_foo._tcp.". The simple instance name must not end in a ".",
  // and the simple service name must end in ".".
  static std::string InstanceFullName(const std::string& instance_name,
                                      const std::string& service_name);

  // Extracts the simple instance name from an instance full name given the
  // name of the service. Returns true and deposits the instance name if
  // successful, return false if not.
  static bool ExtractInstanceName(const std::string& instance_full_name,
                                  const std::string& service_name, std::string* instance_name);

  // Determines if |name| is a local service name matching |service_name| or
  // a subtype of |service_name|. If |name| does specify a subtype, the
  // subtype is returned via |subtype_out|. Otherwise |*subtype_out| is
  // cleared.
  static bool MatchServiceName(const std::string& name, const std::string& service_name,
                               std::string* subtype_out);

  // Determines if |host_name| is a valid host name.
  static bool IsValidHostName(const std::string& host_name);

  // Determines if |service_name| is a valid simple service name.
  static bool IsValidServiceName(const std::string& service_name);

  // Determines if |instance_name| is a valid simple instance name.
  static bool IsValidInstanceName(const std::string& instance_name);

  // Determines if |subtype_name| is a valid simple subtype name.
  static bool IsValidSubtypeName(const std::string& subtype_name);

  // Determines if |text_string| is a valid text string.
  static bool IsValidTextString(const std::string& text_string);

  // Determines if |text_string| is a valid text string.
  static bool IsValidTextString(const std::vector<uint8_t>& text_string);

  // Returns the alternate host name for |host_name|. For example, if |host_name| is
  // "fuchsia-1234-5678-9abc", this method returns "123456789ABC". If |host_name| isn't
  // the expected size (22 characters), this method returns the |host_name| argument.
  // TODO(fxb/113901): Remove this when alt_services is no longer needed.
  static std::string AltHostName(const std::string& host_name);
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_MDNS_NAMES_H_
