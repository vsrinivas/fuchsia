// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/prober.h"

#include <zircon/syscalls.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_delta.h"
#include "src/lib/fxl/time/time_point.h"

namespace mdns {

// static
constexpr fxl::TimeDelta Prober::kMaxProbeInterval =
    fxl::TimeDelta::FromMilliseconds(250);

Prober::Prober(MdnsAgent::Host* host, DnsType type, CompletionCallback callback)
    : MdnsAgent(host), type_(type), callback_(std::move(callback)) {
  FXL_DCHECK(callback_);
}

Prober::~Prober() {}

void Prober::Start(const std::string& host_full_name, inet::IpPort mdns_port) {
  FXL_DCHECK(!host_full_name.empty());

  MdnsAgent::Start(host_full_name, mdns_port);

  host_full_name_ = host_full_name;

  question_ = std::make_shared<DnsQuestion>(ResourceName(), DnsType::kAny);
  question_->unicast_response_ = true;

  Probe(InitialDelay());
}

void Prober::ReceiveResource(const DnsResource& resource,
                             MdnsResourceSection section) {
  if (resource.name_.dotted_string_ != ResourceName()) {
    return;
  }

  if (resource.type_ == type_ ||
      (resource.type_ == DnsType::kAaaa && type_ == DnsType::kA)) {
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
        fxl::TimePoint::Now());
  }
}

fxl::TimeDelta Prober::InitialDelay() {
  uint64_t random = 0;
  zx_cprng_draw(&random, sizeof(random));
  int64_t random_nonnegative_int64 = static_cast<int64_t>(random >> 1);
  FXL_DCHECK(random_nonnegative_int64 >= 0);
  return fxl::TimeDelta::FromNanoseconds(random_nonnegative_int64 %
                                         kMaxProbeInterval.ToNanoseconds());
}

void Prober::Probe(fxl::TimeDelta delay) {
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
          SendQuestion(question_);
          SendProposedResources(MdnsResourceSection::kAuthority);
          Probe(kMaxProbeInterval);
        }
      },
      fxl::TimePoint::Now() + delay);
}

}  // namespace mdns
