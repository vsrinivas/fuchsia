// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_
#define SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <Weave/Core/WeaveError.h>

#include <thread>

namespace weavestack {

class App {
 public:
  App();
  ~App();

  void Quit();
  WEAVE_ERROR Start();
  WEAVE_ERROR Init();
  void RunLoop();
  void Join();
 private:
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  WEAVE_ERROR HandlePackets(void);
  std::thread thread_;
  std::atomic_flag running_ = ATOMIC_FLAG_INIT;
};

}  // namespace weavestack

#endif  // SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_
