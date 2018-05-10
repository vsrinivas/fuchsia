// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include "garnet/lib/debug_ipc/helper/fd_watcher.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
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
  loop.Init();

  loop.PostTask([loop_ptr = &loop]() { loop_ptr->QuitNow(); });
  loop.Run();

  loop.Cleanup();
}

TEST(MessageLoop, WatchPipeFD) {
  // Make a pipe to talk about.
  int pipefd[2] = {-1, -1};
  ASSERT_EQ(0, pipe(pipefd));
  ASSERT_NE(-1, pipefd[0]);
  ASSERT_NE(-1, pipefd[1]);

  int flags = fcntl(pipefd[0], F_GETFD);
  flags |= O_NONBLOCK;
  ASSERT_EQ(0, fcntl(pipefd[0], F_SETFD, flags));

  flags = fcntl(pipefd[1], F_GETFD);
  flags |= O_NONBLOCK;
  ASSERT_EQ(0, fcntl(pipefd[1], F_SETFD, flags));

  class ReadableWatcher : public FDWatcher {
   public:
    explicit ReadableWatcher(MessageLoop* loop) : loop_(loop) {}
    void OnFDReadable(int fd) override { loop_->QuitNow(); }

   private:
    MessageLoop* loop_;
  };

  PlatformMessageLoop loop;
  loop.Init();

  // Scope everything to before MessageLoop::Cleanup().
  {
    ReadableWatcher watcher(&loop);

    // Going to write to pipefd[0] -> read from pipefd[1].
    MessageLoop::WatchHandle watch_handle =
        loop.WatchFD(MessageLoop::WatchMode::kRead, pipefd[0], &watcher);
    ASSERT_TRUE(watch_handle.watching());

    // Enqueue a task that should cause pipefd[1] to become readable.
    loop.PostTask([write_fd = pipefd[1]]() { write(write_fd, "Hello", 5); });

    // This will quit on success because the OnFDReadable callback called
    // QuitNow, or hang forever on failure.
    // TODO(brettw) add a timeout when timers are supported in the message loop.
    loop.Run();
  }
  loop.Cleanup();
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
  loop.Init();

  // Scope everything to before MessageLoop::Cleanup().
  {
    ReadableWatcher watcher(&loop);

    MessageLoop::WatchHandle watch_handle = loop.WatchSocket(
        MessageLoop::WatchMode::kRead, receiver.get(), &watcher);
    ASSERT_TRUE(watch_handle.watching());

    // Enqueue a task that should cause receiver to become readable.
    loop.PostTask([&sender]() { sender.write(0, "Hello", 5, nullptr); });

    // This will quit on success because the OnSocketReadable callback called
    // QuitNow, or hang forever on failure.
    // TODO(brettw) add a timeout when timers are supported in the message loop.
    loop.Run();
  }
  loop.Cleanup();
}
#endif

}  // namespace debug_ipc
