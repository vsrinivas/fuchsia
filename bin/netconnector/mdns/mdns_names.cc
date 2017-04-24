// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/mdns_names.h"

#include "lib/ftl/logging.h"

namespace netconnector {
namespace mdns {

namespace {

std::string Concatenate(std::initializer_list<std::string> strings) {
  std::string result;
  size_t result_size = 0;

  for (auto& string : strings) {
    result_size += string.size();
  }

  result.reserve(result_size);

  for (auto& string : strings) {
    result.append(string);
  }

  return result;
}

}  // namespace

// static
const std::string MdnsNames::kLocalDomainName = "local.";

// static
std::string MdnsNames::LocalHostFullName(const std::string& host_name) {
  FTL_DCHECK(!host_name.empty());
  return Concatenate({host_name, ".", kLocalDomainName});
}

// static
std::string MdnsNames::LocalServiceFullName(const std::string& service_name) {
  FTL_DCHECK(IsValidServiceName(service_name));

  return Concatenate({service_name, kLocalDomainName});
}

// static
std::string MdnsNames::LocalInstanceFullName(const std::string& instance_name,
                                             const std::string& service_name) {
  FTL_DCHECK(!instance_name.empty());
  FTL_DCHECK(IsValidServiceName(service_name));

  return Concatenate({instance_name, ".", service_name, kLocalDomainName});
}

// static
bool MdnsNames::ExtractInstanceName(const std::string& instance_full_name,
                                    const std::string& service_name,
                                    std::string* instance_name) {
  // Long enough?
  size_t size = instance_full_name.size();
  if (size <= service_name.size() + kLocalDomainName.size() + 1) {
    return false;
  }

  // Ends with |kLocalDomainName|?
  size -= kLocalDomainName.size();
  if (instance_full_name.compare(size, kLocalDomainName.size(),
                                 kLocalDomainName) != 0) {
    return false;
  }

  // Ends with |service_name||kLocalDomainName|?
  size -= service_name.size();
  if (instance_full_name.compare(size, service_name.size(), service_name) !=
      0) {
    return false;
  }

  // Ends with .|service_name||kLocalDomainName|?
  --size;
  if (instance_full_name[size] != '.') {
    return false;
  }

  // Well OK then.
  *instance_name = instance_full_name.substr(0, size);

  return true;
}

// static
bool MdnsNames::IsValidServiceName(const std::string& service_name) {
  static const std::string kTcpSuffix = "._tcp.";
  static const std::string kUdpSuffix = "._udp.";

  if (service_name.size() < 8 || service_name[0] != '_') {
    return false;
  }

  return service_name.compare(service_name.size() - kTcpSuffix.size(),
                              kTcpSuffix.size(), kTcpSuffix) == 0 ||
         service_name.compare(service_name.size() - kUdpSuffix.size(),
                              kUdpSuffix.size(), kUdpSuffix) == 0;
}

}  // namespace mdns
}  // namespace netconnector
