// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "garnet/bin/netconnector/ip_address.h"
#include "garnet/bin/netconnector/mdns/mdns.h"
#include "garnet/bin/netconnector/mdns/mdns_agent.h"
#include "lib/fxl/time/time_point.h"

namespace netconnector {
namespace mdns {

// Requests host name resolution.
class HostNameResolver : public MdnsAgent,
                         public std::enable_shared_from_this<HostNameResolver> {
 public:
  // Creates a |HostNameResolver|.
  HostNameResolver(MdnsAgent::Host* host,
                   const std::string& host_name,
                   const std::string& host_full_name,
                   fxl::TimePoint timeout,
                   const Mdns::ResolveHostNameCallback& callback);

  ~HostNameResolver() override;

  // MdnsAgent implementation.
  void Start() override;

  void Wake() override;

  void ReceiveQuestion(const DnsQuestion& question) override;

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section) override;

  void EndOfMessage() override;

  void Quit() override;

 private:
  MdnsAgent::Host* host_;
  std::string host_name_;
  std::string host_full_name_;
  fxl::TimePoint timeout_;
  Mdns::ResolveHostNameCallback callback_;
  IpAddress v4_address_;
  IpAddress v6_address_;
};

}  // namespace mdns
}  // namespace netconnector
