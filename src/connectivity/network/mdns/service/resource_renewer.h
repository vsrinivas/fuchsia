// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_RESOURCE_RENEWER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_RESOURCE_RENEWER_H_

#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <memory>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/connectivity/network/mdns/service/mdns_agent.h"
#include "src/lib/inet/ip_port.h"
#include "src/lib/syslog/cpp/logger.h"

namespace mdns {

// Renews resources by querying them before their TTLs expire.
//
// |ResourceRenewer| renews a set of resources as directed by calls to its
// |Renew| method.
//
// |ResourceRenewer| queries for a resource at 80%, 85%, 90% and 95% of the
// resource's TTL. If a resource is renewed, the renewer forgets about the
// resource until asked again to renew it. If a resource's TTL expires,
// |ResourceRenewer| sends a resource record to all the agents with
// a TTL of zero, signalling that the resource should be deleted and forgets
// about the resource. If a resource is explicitly deleted (a resource
// record arrives with TTL 0), |ResourceRenewer| will not attempt to renew the
// resource.
//
// Agents that need a resource record renewed call |Renew| on the host, which
// then calls |Renew| on the |ResourceRenewer|. Agents must continue to renew
// incoming resources as long as they want renewals to occur. When an agent
// loses interest in a record, it should imply stop renewing the incoming
// resource records. This approach will cause some unneeded renewals, but avoids
// difficult cleanup issues associated with a persistent renewal scheme.
class ResourceRenewer : public MdnsAgent {
 public:
  ResourceRenewer(MdnsAgent::Host* host);

  ~ResourceRenewer() override;

  // Attempts to renew |resource| before its TTL expires.
  void Renew(const DnsResource& resource);

  // MdnsAgent overrides.
  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section) override;

  void Quit() override;

 private:
  // All Entry objects are represented in both |entries_| and |schedule_|. We're
  // using raw pointers, so the destructor must delete all Entry objects
  // explicitly.

  struct Entry {
    static constexpr uint32_t kFirstQueryPerThousand = 800;
    static constexpr uint32_t kQueryIntervalPerThousand = 50;
    static constexpr uint32_t kQueriesToAttempt = 4;

    Entry(const std::string& name, DnsType type) : name_(name), type_(type) {}

    std::string name_;
    DnsType type_;

    zx::time time_;
    zx::duration interval_;
    uint32_t queries_remaining_;

    // Time value used for |schedule|. In some cases, we want to postpone a
    // query or expiration that was previously scheduled. In this case, |time_|
    // will be increased, but |schedule_time_| will remain unchanged. When the
    // entry comes up in the schedule, the entry should be rescheduled if
    // |time_| is different from |schedule_time_|.
    zx::time schedule_time_;

    bool delete_ = false;

    // Sets |time_|, |interval_| and |queries_remaining_| to their initial
    // values to initiate the eventual renewal of the resource.
    void SetFirstQuery(zx::time now, uint32_t time_to_live);

    // Updates |time_| and |queries_remaining_| for the purposes of scheduling
    // the next query or expiration.
    void SetNextQueryOrExpiration();
  };

  struct Hash {
    size_t operator()(const std::unique_ptr<Entry>& m) const {
      FX_DCHECK(m);
      return std::hash<std::string>{}(m->name_) ^ std::hash<DnsType>{}(m->type_);
    }
  };

  struct Equals {
    size_t operator()(const std::unique_ptr<Entry>& a, const std::unique_ptr<Entry>& b) const {
      FX_DCHECK(a);
      FX_DCHECK(b);
      return a->name_ == b->name_ && a->type_ == b->type_;
    }
  };

  struct LaterScheduleTime {
    size_t operator()(const Entry* a, const Entry* b) {
      FX_DCHECK(a != nullptr);
      FX_DCHECK(b != nullptr);
      return a->schedule_time_ > b->schedule_time_;
    }
  };

  // Sends current renewals and schedules another call to |SendRenewals|, as
  // appropriate.
  void SendRenewals();

  void Schedule(Entry* entry);

  void EraseEntry(Entry* entry);

  std::unordered_set<std::unique_ptr<Entry>, Hash, Equals> entries_;
  std::priority_queue<Entry*, std::vector<Entry*>, LaterScheduleTime> schedule_;

 public:
  // Disallow copy, assign and move.
  ResourceRenewer(const ResourceRenewer&) = delete;
  ResourceRenewer(ResourceRenewer&&) = delete;
  ResourceRenewer& operator=(const ResourceRenewer&) = delete;
  ResourceRenewer& operator=(ResourceRenewer&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_RESOURCE_RENEWER_H_
