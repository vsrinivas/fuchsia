// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_
#define SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <thread>

namespace weavestack {

class App {
 public:
  App();
  ~App();

  async::Loop* loop() { return &loop_; }

 private:
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Any state owned by the Weave thread
  class WeaveState;

  using WeaveOp = fit::function<void(WeaveState*)>;

  void WeaveMain();
  // Post op to be run by WeaveMain
  void PostWeaveOp(WeaveOp op) { abort(); /* unimplemented */ }
  // Post op to be run by main thread
  void PostAppOp(fit::closure op) { async::PostTask(loop()->dispatcher(), std::move(op)); }

  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};
  std::thread weave_loop_{[this]() { WeaveMain(); }};
};

}  // namespace weavestack

#endif  // SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_
