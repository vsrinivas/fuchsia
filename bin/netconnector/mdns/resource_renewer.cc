// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/resource_renewer.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"

namespace netconnector {
namespace mdns {

ResourceRenewer::ResourceRenewer(MdnsAgent::Host* host) : MdnsAgent(host) {}

ResourceRenewer::~ResourceRenewer() {
  FXL_DCHECK(entries_.size() == schedule_.size());
  for (Entry* entry : entries_) {
    delete entry;
  }
}

void ResourceRenewer::Renew(const DnsResource& resource) {
  FXL_DCHECK(resource.time_to_live_ != 0);

  Entry key(resource.name_.dotted_string_, resource.type_);
  auto iter = entries_.find(&key);

  if (iter == entries_.end()) {
    Entry* entry = new Entry(resource.name_.dotted_string_, resource.type_);
    entry->SetFirstQuery(resource.time_to_live_);

    Schedule(entry);

    if (entry == schedule_.top()) {
      PostTaskForTime([this]() { SendRenewals(); }, entry->schedule_time_);
    }

    entries_.insert(std::move(entry));
  } else {
    (*iter)->SetFirstQuery(resource.time_to_live_);
    (*iter)->delete_ = false;
  }
}

void ResourceRenewer::ReceiveResource(const DnsResource& resource,
                                      MdnsResourceSection section) {
  FXL_DCHECK(section != MdnsResourceSection::kExpired);

  Entry key(resource.name_.dotted_string_, resource.type_);
  auto iter = entries_.find(&key);
  if (iter != entries_.end()) {
    (*iter)->delete_ = true;
  }
}

void ResourceRenewer::Quit() {
  // This never gets called.
  FXL_DCHECK(false);
}

void ResourceRenewer::SendRenewals() {
  fxl::TimePoint now = fxl::TimePoint::Now();

  while (!schedule_.empty() && schedule_.top()->schedule_time_ <= now) {
    Entry* entry = const_cast<Entry*>(schedule_.top());
    schedule_.pop();

    if (entry->delete_) {
      entries_.erase(entry);
      delete entry;
    } else if (entry->schedule_time_ != entry->time_) {
      // Postponed entry.
      Schedule(entry);
    } else if (entry->queries_remaining_ == 0) {
      // TTL expired.
      std::shared_ptr<DnsResource> resource =
          std::make_shared<DnsResource>(entry->name_, entry->type_);
      resource->time_to_live_ = 0;
      entries_.erase(entry);
      SendResource(resource, MdnsResourceSection::kExpired);
      delete entry;
    } else {
      // Need to query.
      SendQuestion(std::make_shared<DnsQuestion>(entry->name_, entry->type_));
      entry->SetNextQueryOrExpiration();
      Schedule(entry);
    }
  }

  if (!schedule_.empty()) {
    PostTaskForTime([this]() { SendRenewals(); },
                    schedule_.top()->schedule_time_);
  }
}

void ResourceRenewer::Schedule(Entry* entry) {
  entry->schedule_time_ = entry->time_;
  schedule_.push(entry);
}

void ResourceRenewer::Entry::SetFirstQuery(uint32_t time_to_live) {
  time_ = fxl::TimePoint::Now() + fxl::TimeDelta::FromMilliseconds(
                                      time_to_live * kFirstQueryPerThousand);
  interval_ = fxl::TimeDelta::FromMilliseconds(time_to_live *
                                               kQueryIntervalPerThousand);
  queries_remaining_ = kQueriesToAttempt;
}

void ResourceRenewer::Entry::SetNextQueryOrExpiration() {
  FXL_DCHECK(queries_remaining_ != 0);
  time_ = time_ + interval_;
  --queries_remaining_;
}

}  // namespace mdns
}  // namespace netconnector
