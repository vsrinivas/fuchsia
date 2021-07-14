// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_FIT_BRIDGE_MAP_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_FIT_BRIDGE_MAP_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>

#include <cstdint>
#include <map>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/bridge.h"
#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace fit {

// Manages access to multiple Bridge objects, allowing access through an id.
template <typename V = void>
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

  void CompleteError(uint64_t id, Error error) {
    if (Contains(id)) {
      bridges_.at(id).CompleteError(error);
    }
  }

  void CompleteAllError(Error error) {
    for (auto& [_, bridge] : bridges_) {
      bridge.CompleteError(error);
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
  ::fpromise::promise<V, Error> WaitForDone(uint64_t id) {
    if (Contains(id)) {
      return bridges_.at(id).WaitForDone();
    }

    return ::fpromise::make_result_promise<V>(::fpromise::error(Error::kDefault));
  }

  // Start the timeout and get the promise that will be ungated when the bridge at |id| is
  // completed. An error if returned if the bridge doesn't exist.
  ::fpromise::promise<V, Error> WaitForDone(uint64_t id, Timeout timeout) {
    if (Contains(id)) {
      return bridges_.at(id).WaitForDone(std::move(timeout));
    }
    return ::fpromise::make_result_promise<V>(::fpromise::error(Error::kDefault));
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::map<uint64_t, Bridge<V>> bridges_;
  uint64_t next_id_ = 1;
};

}  // namespace fit
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_FIT_BRIDGE_MAP_H_
