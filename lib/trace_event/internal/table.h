// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_EVENT_INTERNAL_TABLE_H_
#define APPS_TRACING_LIB_TRACE_EVENT_INTERNAL_TABLE_H_

#include <mutex>
#include <unordered_map>

namespace tracing {
namespace internal {

template <typename Value, uint16_t null_value, size_t size>
class Table {
 public:
  void Reset() {
    std::lock_guard<std::mutex> lg(guard_);
    table_.clear();
  }

  bool Register(Value object, uint16_t* index) {
    std::lock_guard<std::mutex> lg(guard_);

    auto it = table_.find(object);
    if (it != table_.end()) {
      *index = it->second;
      return false;
    }

    if (table_.size() == size) {
      *index = null_value;
      return false;
    }

    *index = table_[object] = table_.size() + 1;
    return true;
  }

 private:
  std::mutex guard_;
  std::unordered_map<Value, uint16_t> table_;
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_EVENT_INTERNAL_TABLE_H_
