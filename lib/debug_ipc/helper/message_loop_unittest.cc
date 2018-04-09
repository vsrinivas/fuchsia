// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "garnet/lib/debug_ipc/helper/fd_watcher.h"
#include "gtest/gtest.h"

#if defined(__Fuchsia__)
#include <zx/socket.h>

#include "garnet/lib/debug_ipc/helper/socket_watcher.h"
#endif

namespace debug_ipc {

// This test either passes or hangs forever because the post didn't work.
// TODO(brettw) add a timeout when timers are supported in the message loop.
TEST(MessageLoop, PostQuit) {
  PlatformMessageLoop loop;
  loop.PostTask([loop_ptr = &loop]() { loop_ptr->QuitNow(); });
  loop.Run();
}

TEST(MessageLoop, WatchPipeFD) {
  // Make a pipe to talk about.
  int pipefd[2] = { -1, -1 };
  ASSERT_EQ(0, pipe(pipefd));
  ASSERT_NE(-1, pipefd[0]);
  ASSERT_NE(-1, pipefd[1]);

  class ReadableWatcher : public FDWatcher {
   public:
    explicit ReadableWatcher(MessageLoop* loop) : loop_(loop) {}
    void OnFDReadable(int fd) override {
      loop_->QuitNow();
    }
   private:
    MessageLoop* loop_;
  };

  PlatformMessageLoop loop;
  ReadableWatcher watcher(&loop);

  // Going to write to pipefd[0] -> read from pipefd[1].
  MessageLoop::WatchHandle watch_handle =
      loop.WatchFD(MessageLoop::WatchMode::kRead, pipefd[1], &watcher);
  ASSERT_TRUE(watch_handle.watching());

  // Enqueue a task that should cause pipefd[1] to become readable.
  loop.PostTask([write_fd = pipefd[0]]() { write(write_fd, "Hello", 5); });

  // This will quit on success because the OnFDReadable callback called
  // QuitNow, or hang forever on failure.
  // TODO(brettw) add a timeout when timers are supported in the message loop.
  loop.Run();
}

#if defined(__Fuchsia__)
TEST(MessageLoop, ZirconSocket) {
  zx::socket sender, receiver;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &sender, &receiver));

  class ReadableWatcher : public SocketWatcher {
   public:
    explicit ReadableWatcher(MessageLoop* loop) : loop_(loop) {}
    void OnSocketReadable(zx_handle_t socket_handle) override {
      loop_->QuitNow();
    }
   private:
    MessageLoop* loop_;
  };

  PlatformMessageLoop loop;
  ReadableWatcher watcher(&loop);

  MessageLoop::WatchHandle watch_handle =
      loop.WatchSocket(MessageLoop::WatchMode::kRead, receiver.get(), &watcher);
  ASSERT_TRUE(watch_handle.watching());

  // Enqueue a task that should cause receiver to become readable.
  loop.PostTask([&sender]() { sender.write(0, "Hello", 5, nullptr); });

  // This will quit on success because the OnSocketReadable callback called
  // QuitNow, or hang forever on failure.
  // TODO(brettw) add a timeout when timers are supported in the message loop.
  loop.Run();
}
#endif

}  // namespace debug_ipc
