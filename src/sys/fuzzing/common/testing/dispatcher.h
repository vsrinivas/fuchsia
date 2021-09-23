// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_DISPATCHER_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_DISPATCHER_H_

#include <lib/async-loop/cpp/loop.h>

#include <memory>

namespace fuzzing {

// This class simply wraps an async::Loop that is started on its own thread and joined when the
// object is destroyed. This makes it easy to create a FIDL dispatcher with RAII semantics.
class FakeDispatcher final {
 public:
  FakeDispatcher();
  ~FakeDispatcher() = default;

  async_dispatcher_t* get() const;

 private:
  std::unique_ptr<async::Loop> loop_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_DISPATCHER_H_
