// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_HOST_NAME_RESOLVER_H_
#define GARNET_BIN_MDNS_SERVICE_HOST_NAME_RESOLVER_H_

#include <memory>
#include <string>

#include "garnet/bin/mdns/service/mdns.h"
#include "garnet/bin/mdns/service/mdns_agent.h"
#include "garnet/lib/inet/ip_address.h"
#include "src/lib/fxl/time/time_point.h"

namespace mdns {

// Requests host name resolution.
class HostNameResolver : public MdnsAgent {
 public:
  // Creates a |HostNameResolver|.
  HostNameResolver(MdnsAgent::Host* host, const std::string& host_name,
                   fxl::TimePoint timeout,
                   Mdns::ResolveHostNameCallback callback);

  ~HostNameResolver() override;

  // MdnsAgent overrides.
  void Start(const std::string& host_full_name) override;

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section) override;

  void EndOfMessage() override;

  void Quit() override;

 private:
  std::string host_name_;
  std::string host_full_name_;
  fxl::TimePoint timeout_;
  Mdns::ResolveHostNameCallback callback_;
  inet::IpAddress v4_address_;
  inet::IpAddress v6_address_;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_HOST_NAME_RESOLVER_H_
