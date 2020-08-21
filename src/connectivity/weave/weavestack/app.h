// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_
#define SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <sys/select.h>

#include <map>
#include <memory>

#include <Weave/Core/WeaveError.h>
#include <src/lib/fsl/tasks/fd_waiter.h>

#include "fidl/stack_impl.h"

namespace weavestack {

class App {
 public:
  App();
  ~App();

  void Quit(void);
  zx_status_t Init(void);
  zx_status_t Run(zx::time deadline = zx::time::infinite(), bool once = false);
  async::Loop* loop() { return &loop_; }

 private:
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  zx_status_t WaitForFd(int fd, uint32_t events);
  zx_status_t StartFdWaiters(void);
  void ClearWaiters();
  void ClearFds();
  void FdHandler(zx_status_t status, uint32_t zero);
  // Static handler to trampoline callbacks to an App instance.
  // |fd| refers to an fd to be closed.
  // |arg| is a pointer to the App instance.
  static void TrampolineDoClose(int fd, intptr_t arg);
  // DoClose will perform the required cleanup for the given
  // |fd| before closing |fd|. Register DoClose as a pre-socket
  // close handler which gets invoked, whenever openweave-core
  // is about to close a socket.
  void DoClose(int fd);
  struct {
    fd_set read_fds;
    fd_set write_fds;
    fd_set except_fds;
    int num_fds;
  } fds_;
  std::map<int, std::unique_ptr<fsl::FDWaiter>> waiters_;
  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};
  bool initialized_ = false;
  std::unique_ptr<async::TaskClosure> sleep_task_;
  std::unique_ptr<StackImpl> stack_impl_;
};

}  // namespace weavestack

#endif  // SRC_CONNECTIVITY_WEAVE_WEAVESTACK_APP_H_
