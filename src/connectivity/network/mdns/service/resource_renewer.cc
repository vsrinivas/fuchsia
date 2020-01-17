// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/resource_renewer.h"

#include <lib/zx/clock.h>

#include "src/lib/syslog/cpp/logger.h"

namespace mdns {

ResourceRenewer::ResourceRenewer(MdnsAgent::Host* host) : MdnsAgent(host) {}

ResourceRenewer::~ResourceRenewer() { FX_DCHECK(entries_.size() == schedule_.size()); }

void ResourceRenewer::Renew(const DnsResource& resource) {
  FX_DCHECK(resource.time_to_live_ != 0);

  auto key = std::make_unique<Entry>(resource.name_.dotted_string_, resource.type_);
  auto iter = entries_.find(key);

  if (iter == entries_.end()) {
    auto entry = std::make_unique<Entry>(resource.name_.dotted_string_, resource.type_);
    entry->SetFirstQuery(now(), resource.time_to_live_);

    Schedule(entry.get());

    if (entry.get() == schedule_.top()) {
      PostTaskForTime([this]() { SendRenewals(); }, entry->schedule_time_);
    }

    entries_.insert(std::move(entry));
  } else {
    (*iter)->SetFirstQuery(now(), resource.time_to_live_);
    (*iter)->delete_ = false;
  }
}

void ResourceRenewer::ReceiveResource(const DnsResource& resource, MdnsResourceSection section) {
  FX_DCHECK(section != MdnsResourceSection::kExpired);

  auto key = std::make_unique<Entry>(resource.name_.dotted_string_, resource.type_);
  auto iter = entries_.find(key);
  if (iter != entries_.end()) {
    (*iter)->delete_ = true;
  }
}

void ResourceRenewer::Quit() {
  // This never gets called.
  FX_DCHECK(false);
}

void ResourceRenewer::SendRenewals() {
  zx::time now = this->now();

  while (!schedule_.empty() && schedule_.top()->schedule_time_ <= now) {
    Entry* entry = const_cast<Entry*>(schedule_.top());
    schedule_.pop();

    if (entry->delete_) {
      EraseEntry(entry);
    } else if (entry->schedule_time_ != entry->time_) {
      // Postponed entry.
      Schedule(entry);
    } else if (entry->queries_remaining_ == 0) {
      // TTL expired.
      std::shared_ptr<DnsResource> resource =
          std::make_shared<DnsResource>(entry->name_, entry->type_);
      resource->time_to_live_ = 0;
      SendResource(resource, MdnsResourceSection::kExpired, addresses().multicast_reply());
      EraseEntry(entry);
    } else {
      // Need to query.
      SendQuestion(std::make_shared<DnsQuestion>(entry->name_, entry->type_));
      entry->SetNextQueryOrExpiration();
      Schedule(entry);
    }
  }

  if (!schedule_.empty()) {
    PostTaskForTime([this]() { SendRenewals(); }, schedule_.top()->schedule_time_);
  }
}

void ResourceRenewer::Schedule(Entry* entry) {
  entry->schedule_time_ = entry->time_;
  schedule_.push(entry);
}

void ResourceRenewer::EraseEntry(Entry* entry) {
  // We need a unique_ptr to use as a key. We don't own |entry| here, so we
  // have to be careful to release the unique_ptr later.
  auto unique_entry = std::unique_ptr<Entry>(entry);
  entries_.erase(unique_entry);
  unique_entry.release();
}

void ResourceRenewer::Entry::SetFirstQuery(zx::time now, uint32_t time_to_live) {
  time_ = now + zx::msec(time_to_live * kFirstQueryPerThousand);
  interval_ = zx::msec(time_to_live * kQueryIntervalPerThousand);
  queries_remaining_ = kQueriesToAttempt;
}

void ResourceRenewer::Entry::SetNextQueryOrExpiration() {
  FX_DCHECK(queries_remaining_ != 0);
  time_ = time_ + interval_;
  --queries_remaining_;
}

}  // namespace mdns
