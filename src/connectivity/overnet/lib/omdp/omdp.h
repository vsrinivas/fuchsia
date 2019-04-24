// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/omdp/cpp/fidl.h>

#include <map>
#include <queue>
#include <random>
#include <unordered_map>

#include "src/connectivity/overnet/lib/environment/timer.h"
#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"
#include "src/connectivity/overnet/lib/vocabulary/optional.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"

namespace overnet {

class Omdp {
 public:
  static constexpr inline uint64_t kPublishDelayMillis = 500;
  static constexpr inline uint64_t kPublishDelayVarianceMillis = 1500;
  static constexpr inline uint64_t kBlockTimeSeconds = 60;

  static constexpr inline IpAddr kMulticastGroupAddr =
      IpAddr(0xff12, 0, 0, 0, 0, 0, 0, 0x0f08, 0xf08);

  Omdp(uint64_t own_node_id, Timer* timer, std::function<uint64_t()> rng);

  void ScheduleBroadcast();
  virtual void OnNewNode(uint64_t node_id, IpAddr addr) = 0;
  Status Process(IpAddr from_addr, Slice slice);

  uint64_t own_id() const { return own_node_id_; }

 private:
  [[nodiscard]] Status RegisterComplaint(IpAddr from, Status status);
  bool InBlockList(IpAddr from);
  virtual void Broadcast(Slice slice) = 0;
  Slice MakeBeacon();

  Timer* const timer_;
  const uint64_t own_node_id_;
  TimeStamp last_received_broadcast_ = timer_->Now();
  TimeStamp last_sent_broadcast_ = last_received_broadcast_;
  int broadcasts_sent_ = 0;

  template <class T>
  using IpMap = std::unordered_map<IpAddr, T, HashIpAddr, EqIpAddr>;

  IpMap<Timeout> blocked_;
  std::function<uint64_t()> rng_;

  Optional<Timeout> broadcast_timeout_;
};

}  // namespace overnet
