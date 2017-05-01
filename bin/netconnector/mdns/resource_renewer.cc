// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/resource_renewer.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace netconnector {
namespace mdns {

// static
const std::string ResourceRenewer::kName = "##resource renewer##";

ResourceRenewer::ResourceRenewer(MdnsAgent::Host* host) : host_(host) {}

ResourceRenewer::~ResourceRenewer() {
  FTL_DCHECK(entries_.size() == schedule_.size());
  for (Entry* entry : entries_) {
    delete entry;
  }
}

void ResourceRenewer::Renew(const DnsResource& resource) {
  FTL_DCHECK(resource.time_to_live_ != 0);

  Entry key(resource.name_.dotted_string_, resource.type_);
  auto iter = entries_.find(&key);

  if (iter == entries_.end()) {
    Entry* entry = new Entry(resource.name_.dotted_string_, resource.type_);
    entry->SetFirstQuery(resource.time_to_live_);

    Schedule(entry);

    if (entry == schedule_.top()) {
      host_->WakeAt(shared_from_this(), entry->schedule_time_);
    }

    entries_.insert(std::move(entry));
  } else {
    (*iter)->SetFirstQuery(resource.time_to_live_);
    (*iter)->delete_ = false;
  }
}

void ResourceRenewer::Start() {}

void ResourceRenewer::Wake() {
  ftl::TimePoint now = ftl::TimePoint::Now();

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
      host_->SendResource(resource, MdnsResourceSection::kExpired, now);
      delete entry;
    } else {
      // Need to query.
      host_->SendQuestion(
          std::make_shared<DnsQuestion>(entry->name_, entry->type_), now);
      entry->SetNextQueryOrExpiration();
      Schedule(entry);
    }
  }

  if (!schedule_.empty()) {
    host_->WakeAt(shared_from_this(), schedule_.top()->schedule_time_);
  }
}

void ResourceRenewer::ReceiveQuestion(const DnsQuestion& question) {}

void ResourceRenewer::ReceiveResource(const DnsResource& resource,
                                      MdnsResourceSection section) {
  FTL_DCHECK(section != MdnsResourceSection::kExpired);

  Entry key(resource.name_.dotted_string_, resource.type_);
  auto iter = entries_.find(&key);
  if (iter != entries_.end()) {
    (*iter)->delete_ = true;
  }
}

void ResourceRenewer::EndOfMessage() {}

void ResourceRenewer::Quit() {
  // This never gets called.
  FTL_DCHECK(false);
}

void ResourceRenewer::Schedule(Entry* entry) {
  entry->schedule_time_ = entry->time_;
  schedule_.push(entry);
}

void ResourceRenewer::Entry::SetFirstQuery(uint32_t time_to_live) {
  time_ = ftl::TimePoint::Now() + ftl::TimeDelta::FromMilliseconds(
                                      time_to_live * kFirstQueryPerThousand);
  interval_ = ftl::TimeDelta::FromMilliseconds(time_to_live *
                                               kQueryIntervalPerThousand);
  queries_remaining_ = kQueriesToAttempt;
}

void ResourceRenewer::Entry::SetNextQueryOrExpiration() {
  FTL_DCHECK(queries_remaining_ != 0);
  time_ = time_ + interval_;
  --queries_remaining_;
}

}  // namespace mdns
}  // namespace netconnector
