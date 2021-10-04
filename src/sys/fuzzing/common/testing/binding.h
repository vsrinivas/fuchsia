// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_BINDING_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_BINDING_H_

#include <memory>

#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/dispatcher.h"

namespace fuzzing {

// |FakeBinding<T>| is a |Binding<T>| that automatically creates a |Dispatcher|.
template <typename T>
class FakeBinding : public Binding<T> {
 public:
  explicit FakeBinding(T* t) : Binding<T>(t, std::make_shared<Dispatcher>()) {}
  ~FakeBinding() override = default;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_BINDING_H_
