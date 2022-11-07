// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_SCOPED_VALUE_CHANGE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_SCOPED_VALUE_CHANGE_H_

#include <type_traits>
#include <utility>

namespace i915 {

// Sets a variable to a value. Restores the old value when going out of scope.
//
// This implementation is geared towards use in testing code. It helps catch
// usage errors, at the cost of efficiency.
//
// This implementation is not thread-safe. The variable managed by a
// ScopedValueChange must only be used in the thread where the ScopedValueChange
// instance is created. ScopedValueChange instances must not be moved across
// threads.
//
// Each memory location can be covered by at most one ScopedValueChange at a
// time. This limitation is enforced with a ZX_ASSERT(). The limitation removes
// the mental model complexity stemming from having a variable covered by
// multiple ScopedValueChanges with overlapping lifetimes. That complexity is
// considered incompatible with the requirement for simplicity in testing code.
//
// Example usage:
//   namespace {
//   int g_timeout_ms = 1'200;
//   }  // namespace
//   ScopedValueChange<int> SomeSystem::OverrideTimeoutMsForTesting(
//       int timeout_ms) {
//     return ScopedValueChange(g_timeout_ms, timeout_ms);
//   }
//
//   TEST(SomeSystemTest, TimeoutScenario) {
//     ScopedValueChange<int> timeout_change =
//       SomeSystem::OverrideTimeoutMsForTesting(0);
//
//     // `g_timeout_ms` will be zero for the duration of the test.
//
//     // When `timeout_change` goes out of scope, the timeout will be restored
//     // to its initial value.
//   }
//
//   class ComplexSystemTest : public ::testing::Test {
//    public:
//     ComplexSystemTest() :
//         timeout_change_(SomeSystem::OverrideTimeoutMsForTesting(0)) {}
//     ~ComplexSystemTest() override = default;
//
//    protected:
//     ScopedValueChange<int> timeout_change_;
//   };
//
//   TEST(ComplexSystemTest, ComplexTimeoutScenario) {
//     // `g_timeout_ms` will be zero here.
//
//     // This test needs to undo a variable change in a surrounding scope.
//     timeout_change_.reset();
//     timeout_change_ = SomeSystem::OverrideTimeoutMsForTesting(32);
//
//     // `g_timeout_ms` will be 32 for the rest of the test.
//   }
template <typename T>
class ScopedValueChange {
 public:
  // Composite types are not supported because they require extra complexity to
  // the logic for checking that each memory location is covered by at most one
  // ScopedValueChange. We can support composite types in the future, by
  // storing byte ranges [&variable, &variable + sizeof(T)) in an interval tree.
  static_assert(std::is_scalar_v<T>,
                "ScopedValueChange does not currently support composite types");

  // Sets `variable` to `temporary_value` and stashes the original value.
  //
  // The caller must ensure that `variable` outlives the newly created instance.
  // The easiest way to meet this guarantee is to use static variables, whose
  // lifetime extends to the end of the process.
  //
  // The caller must ensure that `variable` is not already covered by another
  // ScopedValueChange instance.
  explicit ScopedValueChange(T& variable, T temporary_value)
      : original_value_(variable), changed_variable_(&variable) {
    *changed_variable_ = std::move(temporary_value);
    AddedChangeTo(changed_variable_);
  }

  ScopedValueChange(const ScopedValueChange&) = delete;
  ScopedValueChange& operator=(const ScopedValueChange&) = delete;

  // Moving allowed so ScopedValueChange can be used as a return type.
  ScopedValueChange(ScopedValueChange&& rhs) noexcept
      : original_value_(std::move(rhs.original_value_)), changed_variable_(rhs.changed_variable_) {
    rhs.changed_variable_ = nullptr;
  }
  ScopedValueChange& operator=(ScopedValueChange&& rhs) noexcept {
    std::swap(original_value_, rhs.original_value_);
    std::swap(changed_variable_, rhs.changed_variable_);
    return *this;
  }

  ~ScopedValueChange() { reset(); }

  // Empties this change, restoring the variable to its initial value.
  //
  // After reset(), this ScopedValueChange will be empty, so it will no longer
  // change the variable when it goes out of scope.
  void reset() {
    if (changed_variable_ != nullptr) {
      *changed_variable_ = std::move(original_value_);
      RemovedChangeTo(changed_variable_);
    }
    changed_variable_ = nullptr;
  }

 private:
  static inline void AddedChangeTo(T* variable);
  static inline void RemovedChangeTo(T* variable);

  T original_value_;     // Valid unless this instance was moved-from.
  T* changed_variable_;  // null if this instance was moved-from.
};

// The entire ScopedValueChange class definition is specialized because member
// variables of type void aren't allowed, even if they're not used. We need a
// member variable of type T to store the original value.
template <>
class ScopedValueChange<void> {
 private:
  template <typename T>
  friend class ScopedValueChange;

  static void AddedChangeTo(void* variable);
  static void RemovedChangeTo(void* variable);
};

// static
template <typename T>
inline void ScopedValueChange<T>::AddedChangeTo(T* variable) {
  ScopedValueChange<void>::AddedChangeTo(static_cast<void*>(variable));
}

// static
template <typename T>
inline void ScopedValueChange<T>::RemovedChangeTo(T* variable) {
  ScopedValueChange<void>::RemovedChangeTo(static_cast<void*>(variable));
}

}  // namespace i915

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_SCOPED_VALUE_CHANGE_H_
