// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/prober.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/syscalls.h>

namespace mdns {

// static
constexpr zx::duration Prober::kMaxProbeInterval = zx::msec(250);

Prober::Prober(MdnsAgent::Owner* owner, DnsType type, Media media, IpVersions ip_versions,
               CompletionCallback callback)
    : MdnsAgent(owner),
      type_(type),
      media_(media),
      ip_versions_(ip_versions),
      callback_(std::move(callback)) {
  FX_DCHECK(callback_);
}

Prober::~Prober() {}

void Prober::Start(const std::string& local_host_full_name) {
  FX_DCHECK(!local_host_full_name.empty());

  local_host_full_name_ = local_host_full_name;

  MdnsAgent::Start(local_host_full_name_);

  question_ = std::make_shared<DnsQuestion>(ResourceName(), DnsType::kAny);
  question_->unicast_response_ = true;

  Probe(InitialDelay());
}

void Prober::ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                             ReplyAddress sender_address) {
  if (!sender_address.Matches(media_) || !sender_address.Matches(ip_versions_) ||
      strcasecmp(resource.name_.dotted_string_.c_str(), ResourceName().c_str()) != 0) {
    return;
  }

  if (resource.type_ == type_ || (resource.type_ == DnsType::kAaaa && type_ == DnsType::kA)) {
    // Conflict detected. We defer the call to |RemoveSelf| and the callback
    // so we aren't calling |RemoveSelf| from |ReceiveResource|.
    PostTaskForTime(
        [this]() {
          CompletionCallback callback = std::move(callback_);
          RemoveSelf();
          // This |Prober| has probably been deleted at this point, so we avoid
          // referencing any members.
          callback(false);
        },
        now());
  }
}

zx::duration Prober::InitialDelay() {
  uint64_t random = 0;
  zx_cprng_draw(&random, sizeof(random));
  int64_t random_nonnegative_int64 = static_cast<int64_t>(random >> 1);
  FX_DCHECK(random_nonnegative_int64 >= 0);
  return zx::nsec(random_nonnegative_int64 % kMaxProbeInterval.to_nsecs());
}

void Prober::Probe(zx::duration delay) {
  PostTaskForTime(
      [this]() {
        if (++probe_attempt_count_ > kMaxProbeAttemptCount) {
          // No conflict detected.
          CompletionCallback callback = std::move(callback_);
          RemoveSelf();
          // This |Prober| has probably been deleted at this point, so
          // we avoid referencing any members.
          callback(true);
        } else {
          SendQuestion(question_, ReplyAddress::Multicast(media_, ip_versions_));
          SendProposedResources(MdnsResourceSection::kAuthority);
          Probe(kMaxProbeInterval);
        }
      },
      now() + delay);
}

}  // namespace mdns
