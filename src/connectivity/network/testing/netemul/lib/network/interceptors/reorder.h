// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_REORDER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_REORDER_H_

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <src/lib/fxl/macros.h>

#include <algorithm>
#include <random>

#include "interceptor.h"

namespace netemul {
namespace interceptor {

inline void ReorderDefault(std::vector<InterceptPacket>* vec) {
  std::shuffle(vec->begin(), vec->end(), std::random_device());
}

// Implements packet reordering emulation.
// Template parameter |REORD| is the reordering function that is called on a
// buffered vector of packets. Defaults to using |ReorderDefault|, can be
// overridden for testing.
template <void REORD(std::vector<InterceptPacket>* vec) = ReorderDefault>
class Reorder : public Interceptor {
 public:
  Reorder(uint32_t max_length, zx::duration tick,
          ForwardPacketCallback callback)
      : Interceptor(std::move(callback)),
        task_([this]() { ForwardPending(); }),
        max_length_(max_length),
        tick_(tick) {}

  void Intercept(InterceptPacket packet) override {
    pending_.push_back(std::move(packet));
    if (pending_.size() >= max_length_) {
      ForwardPending();
    }
    ScheduleTick();
  }

  std::vector<InterceptPacket> Flush() override { return std::move(pending_); }

 private:
  void ScheduleTick() {
    if (!task_.is_pending() && tick_.to_nsecs() != 0 && !pending_.empty()) {
      task_.PostDelayed(async_get_default_dispatcher(), tick_);
    }
  }

  void ForwardPending() {
    REORD(&pending_);
    for (auto& x : pending_) {
      Forward(std::move(x));
    }
    pending_.clear();
  }

  async::TaskClosure task_;
  std::vector<InterceptPacket> pending_;
  uint32_t max_length_;
  zx::duration tick_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Reorder);
};

}  // namespace interceptor
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_REORDER_H_
