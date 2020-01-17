// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_PROBER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_PROBER_H_

#include <lib/fit/function.h>
#include <lib/zx/clock.h>

#include <memory>
#include <string>

#include "src/connectivity/network/mdns/service/mdns_agent.h"

namespace mdns {

// Base class for |AddressProber| and |InstanceProber|.
//
// Probing involves repeatedly sending a probe message. The first probe message
// is sent after a random delta of 0 to 250ms. This prevents synchronized
// probing in case multiple devices are powered on simultaneously. Two more
// probe messages are sent at intervals of 250ms, and we'll wait up to 250ms
// for a response.
//
// A probe message consists of a question record asking for any type of
// resource matching the resource name in question. We're looking for specific
// records types, but the wildcard ANY type is used. The question is marked for
// unicast response. The message also includes our proposed record(s) in the
// authority section.
//
// If we see a matching response before we're done with the probe sequence,
// there's a conflict. If not, the probe has completed successfully.
class Prober : public MdnsAgent {
 public:
  using CompletionCallback = fit::function<void(bool)>;

  // Creates a |Prober|. |type| is the resource type for which we're probing.
  // Use |kA| for address types (A and AAAA).
  Prober(MdnsAgent::Host* host, DnsType type, CompletionCallback callback);

  ~Prober() override;

  // MdnsAgent overrides.
  void Start(const std::string& host_full_name, const MdnsAddresses& addresses) final;

  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section) final;

 protected:
  const std::string& host_full_name() { return host_full_name_; }

  // Returns the name of the resource for which we're probing.
  virtual const std::string& ResourceName() = 0;

  // Sends the proposed resources.
  virtual void SendProposedResources(MdnsResourceSection section) = 0;

 private:
  static const zx::duration kMaxProbeInterval;
  static constexpr uint32_t kMaxProbeAttemptCount = 3;

  // Returns a time delta between 0 and |kMaxProbeInterval|.
  zx::duration InitialDelay();

  // Waits for |delay| and either sends a probe message or signals success and
  // calls |RemoveSelf|.
  void Probe(zx::duration delay);

  DnsType type_;
  CompletionCallback callback_;
  std::string host_full_name_;
  std::shared_ptr<DnsQuestion> question_;
  uint32_t probe_attempt_count_ = 0;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_PROBER_H_
