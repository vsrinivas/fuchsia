// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_BRIDGE_MAP_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_BRIDGE_MAP_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>

#include <cstdint>
#include <map>

#include "src/developer/feedback/utils/bridge.h"

namespace feedback {

// Manages access to multiple Bridge objects, allowing access through an id.
template <typename V = void, typename E = void>
class BridgeMap {
 public:
  BridgeMap(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  uint64_t NewBridgeForTask(const std::string& task_name) {
    bridges_.emplace(std::piecewise_construct, std::forward_as_tuple(next_id_),
                     std::forward_as_tuple(dispatcher_, task_name));
    return next_id_++;
  }

  void Delete(uint64_t id) { bridges_.erase(id); }

  bool Contains(uint64_t id) const { return bridges_.find(id) != bridges_.end(); }

  bool IsEmpty() const { return bridges_.empty(); }

  template <typename VV = V, typename = std::enable_if_t<std::is_void_v<VV>>>
  void CompleteOk(uint64_t id) {
    if (Contains(id)) {
      bridges_.at(id).CompleteOk();
    }
  }
  template <typename VV = V, typename = std::enable_if_t<!std::is_void_v<VV>>>
  void CompleteOk(uint64_t id, VV value) {
    if (Contains(id)) {
      bridges_.at(id).CompleteOk(std::move(value));
    }
  }

  template <typename VV = V, typename = std::enable_if_t<std::is_void_v<VV>>>
  void CompleteAllOk() {
    for (auto& [_, bridge] : bridges_) {
      bridge.CompleteOk();
    }
  }
  template <typename VV = V, typename = std::enable_if_t<!std::is_void_v<VV>>>
  void CompleteAllOk(VV value) {
    for (auto& [_, bridge] : bridges_) {
      bridge.CompleteOk(value);
    }
  }

  void CompleteError(uint64_t id) {
    if (Contains(id)) {
      bridges_.at(id).CompleteError();
    }
  }

  void CompleteAllError() {
    for (auto& [_, bridge] : bridges_) {
      bridge.CompleteError();
    }
  }

  bool IsAlreadyDone(uint64_t id) const {
    if (Contains(id)) {
      return bridges_.at(id).IsAlreadyDone();
    }

    // A bridge that isn't in the map is considered done.
    return true;
  }

  // Get the promise that will be ungated when the bridge at |id| is completed. An error is returned
  // if the bridge doesn't exist.
  fit::promise<V, E> WaitForDone(uint64_t id) {
    if (Contains(id)) {
      return bridges_.at(id).WaitForDone();
    }

    return fit::make_result_promise<V, E>(fit::error());
  }

  // Start the timeout and get the promise that will be ungated when the bridge at |id| is
  // completed. An error if returned if the bridge doesn't exist.
  fit::promise<V, E> WaitForDone(
      uint64_t id, zx::duration timeout, fit::closure if_timeout = [] {}) {
    if (Contains(id)) {
      return bridges_.at(id).WaitForDone(timeout, std::move(if_timeout));
    }
    return fit::make_result_promise<V, E>(fit::error());
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::map<uint64_t, Bridge<V, E>> bridges_;
  uint64_t next_id_ = 1;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_BRIDGE_MAP_H_
