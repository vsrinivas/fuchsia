// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "apps/netconnector/src/ip_port.h"
#include "apps/netconnector/src/mdns/mdns_agent.h"

namespace netconnector {
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
class ResourceRenewer : public MdnsAgent,
                        public std::enable_shared_from_this<ResourceRenewer> {
 public:
  static const std::string kName;

  ResourceRenewer(MdnsAgent::Host* host);

  ~ResourceRenewer() override;

  void Renew(const DnsResource& resource);

  // MdnsAgent implementation.
  void Start() override;

  void Wake() override;

  void ReceiveQuestion(const DnsQuestion& question) override;

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section) override;

  void EndOfMessage() override;

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

    ftl::TimePoint time_;
    ftl::TimeDelta interval_;
    uint32_t queries_remaining_;

    // Time value used for |schedule|. In some cases, we want to postpone a
    // query or expiration that was previously scheduled. In this case, |time_|
    // will be increased, but |schedule_time_| will remain unchanged. When the
    // entry comes up in the schedule, the entry should be rescheduled if
    // |time_| is different from |schedule_time_|.
    ftl::TimePoint schedule_time_;

    bool delete_ = false;

    // Sets |time_|, |interval_| and |queries_remaining_| to their initial
    // values to initiate the eventual renewal of the resource.
    void SetFirstQuery(uint32_t time_to_live);

    // Updates |time_| and |queries_remaining_| for the purposes of scheduling
    // the next query or expiration.
    void SetNextQueryOrExpiration();
  };

  struct Hash {
    size_t operator()(const Entry* m) {
      FTL_DCHECK(m != nullptr);
      return std::hash<std::string>{}(m->name_) ^
             std::hash<DnsType>{}(m->type_);
    }
  };

  struct Equals {
    size_t operator()(const Entry* a, const Entry* b) {
      FTL_DCHECK(a != nullptr);
      FTL_DCHECK(b != nullptr);
      return a->name_ == b->name_ && a->type_ == b->type_;
    }
  };

  struct LaterScheduleTime {
    size_t operator()(const Entry* a, const Entry* b) {
      FTL_DCHECK(a != nullptr);
      FTL_DCHECK(b != nullptr);
      return a->schedule_time_ > b->schedule_time_;
    }
  };

  void Schedule(Entry* entry);

  MdnsAgent::Host* host_;
  std::unordered_set<Entry*, Hash, Equals> entries_;
  std::priority_queue<Entry*, std::vector<Entry*>, LaterScheduleTime> schedule_;
};

}  // namespace mdns
}  // namespace netconnector
