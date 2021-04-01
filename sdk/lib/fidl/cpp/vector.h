// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_VECTOR_H_
#define LIB_FIDL_CPP_VECTOR_H_

#include <lib/fidl/cpp/comparison.h>
#include <lib/stdcompat/optional.h>
#include <zircon/assert.h>

#include <utility>
#include <vector>

#include "lib/fidl/cpp/traits.h"

namespace fidl {

template <typename T>
class VectorPtr : public cpp17::optional<std::vector<T>> {
 public:
  constexpr VectorPtr() = default;

  constexpr VectorPtr(cpp17::nullopt_t) noexcept {}
  // Deprecated in favor of cpp17::nullopt_t.
  constexpr VectorPtr(std::nullptr_t) noexcept {}

  VectorPtr(const VectorPtr&) = default;
  VectorPtr& operator=(const VectorPtr&) = default;

  VectorPtr(VectorPtr&&) noexcept = default;
  VectorPtr& operator=(VectorPtr&&) noexcept = default;

  // Move construct and move assignment from the value type
  constexpr VectorPtr(std::vector<T>&& value) : cpp17::optional<std::vector<T>>(std::move(value)) {}
  constexpr VectorPtr& operator=(std::vector<T>&& value) {
    cpp17::optional<std::vector<T>>::operator=(std::move(value));
    return *this;
  }

  // Copy construct and copy assignment from the value type
  constexpr VectorPtr(const std::vector<T>& value) : cpp17::optional<std::vector<T>>(value) {}
  constexpr VectorPtr& operator=(const std::vector<T>& value) {
    cpp17::optional<std::vector<T>>::operator=(value);
    return *this;
  }

  explicit VectorPtr(size_t size) : cpp17::optional<std::vector<T>>(size) {}

  // Override unchecked accessors with versions that check.
  constexpr std::vector<T>* operator->() {
    if (!cpp17::optional<std::vector<T>>::has_value()) {
      __builtin_trap();
    }
    return cpp17::optional<std::vector<T>>::operator->();
  }
  constexpr const std::vector<T>* operator->() const {
    if (!cpp17::optional<std::vector<T>>::has_value()) {
      __builtin_trap();
    }
    return cpp17::optional<std::vector<T>>::operator->();
  }

  VectorPtr& emplace() {
    *this = std::move(std::vector<T>());
    return *this;
  }

  VectorPtr& emplace(std::initializer_list<std::vector<T>>&& ilist) {
    *this = (std::move(std::vector<T>(ilist)));
    return *this;
  }

  VectorPtr& emplace(std::vector<T>&& value) {
    *this = std::move(value);
    return *this;
  }
};

template <class T>
struct Equality<VectorPtr<T>> {
  bool operator()(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) const {
    if (!lhs.has_value() || !rhs.has_value()) {
      return !lhs.has_value() == !rhs.has_value();
    }
    return ::fidl::Equality<std::vector<T>>{}(lhs.value(), rhs.value());
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_VECTOR_H_
