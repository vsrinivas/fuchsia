// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_DELAYING_FACADE_H_
#define PERIDOT_BIN_LEDGER_APP_DELAYING_FACADE_H_

#include <tuple>
#include <vector>

#include <lib/fit/function.h>
#include <lib/fxl/functional/apply.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>

namespace ledger {

// A delaying facade.
//
// Stores the methods to be called on an object, together with their arguments,
// once that object becomes available. An example of usage is the following:
//
// For an object of type SomeClass:
//     class SomeClass {
//       void doSomething(int a, string b);
//     };
//
// We would declare a DelayingFacade:
//    DelayingFacade<SomeClass> df;
//    df.EnqueueCall(&SomeClass::doSomething, 100, "foo");
//    df.EnqueueCall(&SomeClass::doSomething, 200, "bar");
//    // None of the above is executed, yet.
//
//    SomeClass object;
//    df.SetTargetObject(&object); // All previous operations are now executed.
//
//    // The operations following a SetTargetObject are executed immediately.
//    df.EnqueueCall(&SomeClass::doSomething, 300, "baz");
template <typename A>
class DelayingFacade {
 public:
  // Adds a method to be called on an object, with the given arguments, once
  // that object becomes available. If the target object was previously set, the
  // method will be called directly.
  template <typename... Args>
  void EnqueueCall(void (A::*function_pointer)(Args...), Args... args) {
    if (object_) {
      (object_->*function_pointer)(std::forward<Args>(args)...);
      return;
    }
    delayed_calls_.push_back(
        [function_pointer,
         tuple = std::make_tuple(std::forward<Args>(args)...)](A* a) mutable {
          fxl::Apply(
              [&](auto... args) {
                (a->*function_pointer)(std::forward<Args>(args)...);
              },
              std::move(tuple));
        });
  }

  // Sets the target object on which all methods will be executed. This includes
  // methods added from previous calls to Front, and also all future ones.
  void SetTargetObject(A* object) {
    // Check that the object was not set before.
    FXL_DCHECK(!object_);
    FXL_DCHECK(object);
    object_ = object;
    auto calls = std::move(delayed_calls_);
    for (auto& f : calls) {
      f(object);
    }
  }

 private:
  std::vector<fit::function<void(A*)>> delayed_calls_;
  A* object_ = nullptr;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_DELAYING_FACADE_H_
