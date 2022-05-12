// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/resource_renewer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

namespace mdns {
namespace {

Media Union(Media a, Media b) {
  if (a == b) {
    return a;
  }

  return Media::kBoth;
}

IpVersions Union(IpVersions a, IpVersions b) {
  if (a == b) {
    return a;
  }

  return IpVersions::kBoth;
}

}  // namespace

ResourceRenewer::ResourceRenewer(MdnsAgent::Owner* owner) : MdnsAgent(owner) {}

ResourceRenewer::~ResourceRenewer() { FX_DCHECK(entries_.size() == schedule_.size()); }

void ResourceRenewer::Renew(const DnsResource& resource, Media media, IpVersions ip_versions) {
  FX_DCHECK(resource.time_to_live_ != 0);

  auto entry =
      std::make_unique<Entry>(resource.name_.dotted_string_, resource.type_, media, ip_versions);
  auto iter = entries_.find(entry);

  if (iter == entries_.end()) {
    entry->SetFirstQuery(now(), resource.time_to_live_);

    Schedule(entry.get());

    if (entry.get() == schedule_.top()) {
      PostTaskForTime([this]() { SendRenewals(); }, entry->schedule_time_);
    }

    entries_.insert(std::move(entry));
  } else {
    (*iter)->SetFirstQuery(now(), resource.time_to_live_);
    (*iter)->delete_ = false;
    (*iter)->media_ = Union((*iter)->media_, media);
    (*iter)->ip_versions_ = Union((*iter)->ip_versions_, ip_versions);
  }
}

void ResourceRenewer::ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                                      ReplyAddress sender_address) {
  FX_DCHECK(section != MdnsResourceSection::kExpired);

  // |key| is just used as a key, so media and ip_versions are irrelevant.
  auto key = std::make_unique<Entry>(resource.name_.dotted_string_, resource.type_, Media::kBoth,
                                     IpVersions::kBoth);
  auto iter = entries_.find(key);
  if (iter != entries_.end()) {
    if (sender_address.Matches((*iter)->media_) && sender_address.Matches((*iter)->ip_versions_)) {
      (*iter)->delete_ = true;
    }
  }
}

void ResourceRenewer::Quit() {
  // This is never called.
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
      SendResource(resource, MdnsResourceSection::kExpired,
                   ReplyAddress::Multicast(entry->media_, entry->ip_versions_));
      EraseEntry(entry);
    } else {
      // Need to query.
      SendQuestion(std::make_shared<DnsQuestion>(entry->name_, entry->type_),
                   ReplyAddress::Multicast(entry->media_, entry->ip_versions_));
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
