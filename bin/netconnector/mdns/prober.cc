// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/prober.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/random/rand.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

namespace netconnector {
namespace mdns {

// static
constexpr fxl::TimeDelta Prober::kMaxProbeInterval =
    fxl::TimeDelta::FromMilliseconds(250);

Prober::Prober(MdnsAgent::Host* host,
               DnsType type,
               const CompletionCallback& callback)
    : MdnsAgent(host), type_(type), callback_(callback) {
  FXL_DCHECK(callback_);
}

Prober::~Prober() {}

void Prober::Start(const std::string& host_full_name) {
  FXL_DCHECK(!host_full_name.empty());
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
          CompletionCallback callback = callback_;
          RemoveSelf();
          // This |Prober| has probably been deleted at this point, so we avoid
          // referencing any members.
          callback(false);
        },
        fxl::TimePoint::Now());
  }
}

fxl::TimeDelta Prober::InitialDelay() {
  int64_t random_nonnegative_int64 =
      static_cast<int64_t>(fxl::RandUint64() >> 1);
  FXL_DCHECK(random_nonnegative_int64 >= 0);
  return fxl::TimeDelta::FromNanoseconds(random_nonnegative_int64 %
                                         kMaxProbeInterval.ToNanoseconds());
}

void Prober::Probe(fxl::TimeDelta delay) {
  PostTaskForTime(
      [this]() {
        if (++probe_attempt_count_ > kMaxProbeAttemptCount) {
          // No conflict detected.
          CompletionCallback callback = callback_;
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
}  // namespace netconnector
