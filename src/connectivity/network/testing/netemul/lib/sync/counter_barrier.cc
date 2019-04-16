// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "counter_barrier.h"

#include "callback_watch.h"
#include "sync_manager.h"

namespace netemul {

class CounterBarrierWatch : public CallbackWatch<CounterBarrier::Callback> {
 public:
  CounterBarrierWatch(CounterBarrier* parent, uint32_t count,
                      CounterBarrier::Callback callback)
      : CallbackWatch(std::move(callback)), count_(count), parent_(parent) {}

  void OnTimeout() override {
    FireCallback(false);
    parent_->CleanWatches();
  }

  uint32_t count() const { return count_; }

 private:
  uint32_t count_;
  // pointer to parent barrier, not owned
  CounterBarrier* parent_;
};

void CounterBarrier::AddWatch(uint32_t trigger_count, int64_t timeout,
                              Callback callback) {
  auto watch = std::make_unique<CounterBarrierWatch>(this, trigger_count,
                                                     std::move(callback));
  if (timeout > 0) {
    watch->PostTimeout(dispatcher_, timeout);
  }
  watches_.push_back(std::move(watch));
  auto watch_count = count();

  for (auto i = watches_.begin(); i != watches_.end();) {
    if (watch_count >= (*i)->count()) {
      (*i)->FireCallback(true);
      i = watches_.erase(i);
    } else {
      ++i;
    }
  }
}

void CounterBarrier::CleanWatches() {
  for (auto i = watches_.begin(); i != watches_.end();) {
    if ((*i)->valid()) {
      ++i;
    } else {
      i = watches_.erase(i);
    }
  }
}

bool CounterBarrier::empty() const { return watches_.empty(); }
size_t CounterBarrier::count() const { return watches_.size(); }

CounterBarrier::CounterBarrier(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

CounterBarrier::~CounterBarrier() = default;

}  // namespace netemul