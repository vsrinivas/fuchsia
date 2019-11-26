// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_VALUE_LIST_H_
#define LIB_INSPECT_CPP_VALUE_LIST_H_

#include <lib/fit/variant.h>
#include <lib/inspect/cpp/vmo/types.h>

namespace inspect {

namespace internal {
// Base class for ValueHolder types, which approximate std::any.
struct BaseHolder {
  virtual ~BaseHolder() = default;
};

// Holder for an arbitrary type.
template <typename T>
struct ValueHolder : public BaseHolder {
  explicit ValueHolder(T val) : value(std::move(val)) {}
  ~ValueHolder() override = default;
  T value;
};

}  // namespace internal

// A ValueList is a holder for arbitrary values that do not need to be explicitly named or modified
// after creation.
//
// This class is not thread-safe, and it requires external synchronization if accessed from multiple
// threads.
//
// Example:
//   struct Item {
//     // The inspect::Node for this item.
//     Node node;
//
//     // List of unnamed values that should be retained for this item.
//     ValueList values;
//
//     Item(Node* parent, const std::string& name, int value) {
//        node = parent->CreateChild(name);
//        // Expose the value, but enlist it in the ValueList so it doesn't need a name.
//        node.CreateInt("value", value, &values);
//        // "Stats" computes and stores some stats under the node it is given. Keep this in the
//        // ValueList as well since it doesn't need a name.
//        values.emplace(Stats(this, node.CreateChild("stats")));
//     }
//   }
class ValueList final {
 public:
  ValueList() = default;

  // Movable but not copyable.
  ValueList(const ValueList&) = delete;
  ValueList(ValueList&& other) = default;
  ValueList& operator=(const ValueList&) = delete;
  ValueList& operator=(ValueList&& other) = default;

  // Emplaces a value in this ValueList.
  template <typename T>
  void emplace(T value) {
    values_.emplace_back(std::make_unique<internal::ValueHolder<T>>(std::move(value)));
  }

 private:
  // The list of values.
  std::vector<std::unique_ptr<internal::BaseHolder>> values_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_CPP_VALUE_LIST_H_
