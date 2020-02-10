// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_
#define SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

namespace weavestack {

class App {
 public:
  App();

  async::Loop* loop() { return &loop_; }

 private:
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};
};

}  // namespace weavestack

#endif  // SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_
