// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/omdp/omdp.h"

#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/omdp/formatting.h"
#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"

namespace overnet {

Omdp::Omdp(uint64_t own_node_id, Timer* timer, std::function<uint64_t()> rng)
    : timer_(timer), own_node_id_(own_node_id), rng_(rng) {}

Status Omdp::Process(IpAddr from_addr, Slice slice) {
  if (InBlockList(from_addr)) {
    return Status(StatusCode::FAILED_PRECONDITION, "In block list");
  }

  auto parse_status = Decode<fuchsia::overnet::omdp::Beacon>(slice);

  OVERNET_TRACE(DEBUG) << "Parsed OMDP: " << parse_status;

  if (parse_status.is_error()) {
    return RegisterComplaint(from_addr, parse_status.AsStatus());
  }

  if (parse_status->node_id == own_node_id_) {
    return Status::Ok();
  }

  last_received_broadcast_ = timer_->Now();
  OnNewNode(parse_status->node_id, from_addr);
  ScheduleBroadcast();

  return Status::Ok();
}

bool Omdp::InBlockList(IpAddr from) { return blocked_.count(from) != 0; }

Status Omdp::RegisterComplaint(IpAddr from, Status status) {
  assert(!InBlockList(from));
  blocked_.emplace(
      std::piecewise_construct, std::forward_as_tuple(from),
      std::forward_as_tuple(
          timer_, timer_->Now() + TimeDelta::FromSeconds(kBlockTimeSeconds),
          StatusCallback(ALLOCATED_CALLBACK,
                         [this, from](const Status& status) {
                           if (status.is_ok()) {
                             blocked_.erase(from);
                           }
                         })));
  return status;
}

void Omdp::ScheduleBroadcast() {
  auto last_action = std::max(last_received_broadcast_, last_sent_broadcast_);
  auto square = [](int a) { return a * a; };
  auto delay = TimeDelta::FromMilliseconds(square(broadcasts_sent_ + 1) *
                                               kPublishDelayMillis +
                                           rng_() % kPublishDelayMillis);
  OVERNET_TRACE(DEBUG) << "Schedule broadcast for: " << (last_action + delay);
  broadcast_timeout_.Reset(timer_, last_action + delay,
                           [this](const Status& status) {
                             if (status.is_error()) {
                               return;
                             }
                             broadcasts_sent_++;
                             last_sent_broadcast_ = timer_->Now();
                             Broadcast(MakeBeacon());
                             ScheduleBroadcast();
                           });
}

Slice Omdp::MakeBeacon() {
  fuchsia::overnet::omdp::Beacon beacon{own_node_id_};
  return std::move(*Encode(&beacon));
}

}  // namespace overnet
