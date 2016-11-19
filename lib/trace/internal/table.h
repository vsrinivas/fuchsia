// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_TABLE_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_TABLE_H_

#include <mutex>
#include <unordered_map>

namespace tracing {
namespace internal {

template <typename Value, typename Index, size_t size>
class Table {
 public:
  void Reset() {
    std::lock_guard<std::mutex> lg(guard_);
    table_.clear();
  }

  Index Register(Value object, bool* out_added) {
    std::lock_guard<std::mutex> lg(guard_);

    auto it = table_.find(object);
    if (it != table_.end()) {
      *out_added = false;
      return it->second;
    }

    if (table_.size() == size) {
      *out_added = false;
      return 0;
    }

    *out_added = true;
    return table_[object] = table_.size() + 1;
  }

 private:
  std::mutex guard_;
  std::unordered_map<Value, Index> table_;
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_TABLE_H_
