// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_SYNC_COUNTER_BARRIER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_SYNC_COUNTER_BARRIER_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <memory>
#include <vector>

namespace netemul {

class CounterBarrierWatch;
class CounterBarrier {
 public:
  using Callback = fit::function<void(bool)>;

  explicit CounterBarrier(async_dispatcher_t* dispatcher);
  ;
  ~CounterBarrier();

  void AddWatch(uint32_t count, int64_t timeout, Callback callback);

  bool empty() const;
  size_t count() const;

 protected:
  friend class CounterBarrierWatch;
  void CleanWatches();

 private:
  async_dispatcher_t* dispatcher_;
  std::vector<std::unique_ptr<CounterBarrierWatch>> watches_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_SYNC_COUNTER_BARRIER_H_
