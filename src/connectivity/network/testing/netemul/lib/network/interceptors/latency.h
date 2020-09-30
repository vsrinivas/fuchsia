// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_LATENCY_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_LATENCY_H_

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <random>

#include "interceptor.h"
#include "src/lib/fxl/macros.h"

namespace netemul {
namespace interceptor {

// Default generator for random distributions.
class RandomNormalDistribution {
 public:
  RandomNormalDistribution(uint64_t mean, uint64_t stddev)
      : dist_(static_cast<double>(mean), static_cast<double>(stddev)) {}

  int64_t Next() { return static_cast<int64_t>(dist_(dev_)); }

 private:
  std::random_device dev_;
  std::normal_distribution<double> dist_;
};

// Latency network emulation.
// Template parameter |RNG| can be set for testing.
template <typename RNG = RandomNormalDistribution>
class Latency : public Interceptor {
 private:
  class PendingPacket {
   public:
    PendingPacket(Latency* parent, InterceptPacket packet, zx::duration delay)
        : parent_(parent),
          packet_(std::move(packet)),
          task_([this]() { parent_->ForwardPending(this); }) {
      task_.PostDelayed(async_get_default_dispatcher(), delay);
    }

    InterceptPacket TakePacket() { return std::move(packet_); }

   private:
    // pointer to parent, not owned.
    Latency* parent_;
    InterceptPacket packet_;
    async::TaskClosure task_;

    FXL_DISALLOW_COPY_AND_ASSIGN(PendingPacket);
  };

 public:
  // Builds a latency interceptor with mean |mean| and standard deviation
  // |stddev| both expressed in ms.
  Latency(uint64_t mean, uint64_t stddev, ForwardPacketCallback callback)
      : Interceptor(std::move(callback)), random_dist_(mean, stddev) {}

  void Intercept(InterceptPacket packet) override {
    pending_.push_back(
        std::make_unique<PendingPacket>(this, std::move(packet), zx::msec(random_dist_.Next())));
  }

  std::vector<InterceptPacket> Flush() override {
    std::vector<InterceptPacket> packets;
    for (auto& x : pending_) {
      packets.push_back(x->TakePacket());
    }
    pending_.clear();
    return packets;
  }

  void ForwardPending(PendingPacket* pending) {
    auto packet = pending->TakePacket();
    for (auto i = pending_.begin(); i != pending_.end(); i++) {
      if (i->get() == pending) {
        pending_.erase(i);
        break;
      }
    }
    Forward(std::move(packet));
  }

 private:
  RNG random_dist_;
  std::vector<std::unique_ptr<PendingPacket>> pending_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Latency);
};

}  // namespace interceptor
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_LATENCY_H_
