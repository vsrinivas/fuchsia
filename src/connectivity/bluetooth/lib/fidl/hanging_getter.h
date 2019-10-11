// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_LIB_FIDL_HANGING_GETTER_H_
#define SRC_CONNECTIVITY_BLUETOOTH_LIB_FIDL_HANGING_GETTER_H_

#include <lib/fit/function.h>
#include <zircon/assert.h>

#include <optional>

namespace bt_lib_fidl {

// HangingGetter generalizes the consumer/producer pattern often involved in FIDL hanging get
// implementations.
//
// USAGE:
//
// Use `Set()` to update the state watched by the FIDL method:
//
//   HangingGetter<int> foo;
//   ...
//   void OnFooUpdated(int new_foo) {
//     foo.Set(new_foo);
//   }
//
// Use `Watch()` to invoke the response callback with any updates to the state or defer it until the
// update happens later:
//
//   void GetFoo(GetFooCallback callback) {
//     foo.Watch(std::move(callback));
//   }
//
// A specialization is provided for state that is a growing collection of state updates:
//
//   HangingVectorGetter<int> foo;
//   ...
//   void OnFooUpdated(int new_foo) {
//     foo.Add(new_foo);
//   }

template <typename T, typename C = void(T)>
class HangingGetterBase {
 public:
  using Callback = fit::callback<C>;
  using Mutator = fit::function<T(T)>;

  // Returns true if a callback is already assigned to this getter.
  bool armed() const { return static_cast<bool>(callback_); }

  // Assign |value| to the stored state and notify any pending Watch callbacks.
  void Set(T value) {
    value_.emplace(std::move(value));
    WatchInternal();
  }

  // Mutably access the stored state via function |f| and notify any pending Watch callbacks. The
  // Mutator function |f| will receive the current value and should the return the new value after
  // applying any transformations to it.
  //
  // This is useful for value types that accumulate data. To directly assign a value, use the
  // non-mutator overload instead.
  void Transform(Mutator f) {
    ZX_DEBUG_ASSERT(f);

    // Initialize the value if it's not dirty so that the mutator can work with it.
    if (!value_) {
      value_.emplace();
    }

    value_.emplace(f(std::move(value_.value())));
    WatchInternal();
  }

  // Invoke |callback| with any updates to the state. If the state has been accessed via one of the
  // Set() functions since the last call to Watch(), then |callback| will run immediately.
  // Otherwise, |callback| is run next time the state is accessed.
  //
  // Once |callback| runs, the store state becomes cleared. The next call to one of the Set()
  // functions will default-construct a new state.
  bool Watch(Callback callback) {
    if (callback_) {
      return false;
    }

    callback_ = std::move(callback);
    WatchInternal();

    return true;
  }

 protected:
  Callback* callback() { return &callback_; }

  // This member function is called when a value update triggers a registered watcher Callback to be
  // notified. This is abstract to allow HangingGetter variants to apply any necessary
  // transformations between the stored value type "T" and the parameter type of the callback type
  // "C".
  //
  // For example, this is useful when the stored type is a custom accumulator type. A
  // HangingGetterBase implementation can, for example, implement a custom mapping from the stored
  // accumulator to a FIDL struct type that the callback expects.
  virtual void Notify(T&& value) = 0;

 private:
  void WatchInternal() {
    if (callback_ && value_) {
      auto v = std::move(*value_);
      value_.reset();
      Notify(std::move(v));
    }
  }

  std::optional<T> value_;
  Callback callback_;
};

// HangingGetter type where the stored type is identical to the callback parameter type.
template <typename T>
class HangingGetter : public HangingGetterBase<T> {
 protected:
  void Notify(T&& value) override { (*HangingGetterBase<T>::callback())(std::forward<T>(value)); }
};

template <typename T>
class HangingVectorGetter : public HangingGetter<std::vector<T>> {
 public:
  // Insert |value| to the vector and notify any pending Watch callbacks.
  void Add(T&& value) {
    this->Transform([&](auto current) {
      current.push_back(std::forward<T>(value));
      return current;
    });
  }
};

}  // namespace bt_lib_fidl

#endif  // SRC_CONNECTIVITY_BLUETOOTH_LIB_FIDL_HANGING_GETTER_H_
