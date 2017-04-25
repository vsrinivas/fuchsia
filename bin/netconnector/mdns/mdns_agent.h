// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/netconnector/src/mdns/dns_message.h"
#include "lib/ftl/time/time_point.h"

namespace netconnector {
namespace mdns {

enum class MdnsResourceSection { kAnswer, kAuthority, kAdditional };

// Abstract class for objects that drive mDNS question and record traffic.
class MdnsAgent {
 public:
  class Host {
   public:
    virtual ~Host() {}

    virtual void WakeAt(std::shared_ptr<MdnsAgent> agent,
                        ftl::TimePoint when) = 0;

    virtual void SendQuestion(std::shared_ptr<DnsQuestion> question,
                              ftl::TimePoint when) = 0;

    virtual void SendResource(std::shared_ptr<DnsResource> resource,
                              MdnsResourceSection section,
                              ftl::TimePoint when) = 0;

    virtual void SendAddresses(MdnsResourceSection section,
                               ftl::TimePoint when) = 0;

    virtual void RemoveAgent(const std::string& name) = 0;
  };

  virtual ~MdnsAgent() {}

  virtual void Start() = 0;

  virtual void Wake() = 0;

  virtual void ReceiveQuestion(const DnsQuestion& question) = 0;

  virtual void ReceiveResource(const DnsResource& resource,
                               MdnsResourceSection section) = 0;

  virtual void EndOfMessage() = 0;

  virtual void Quit() = 0;
};

}  // namespace mdns
}  // namespace netconnector
