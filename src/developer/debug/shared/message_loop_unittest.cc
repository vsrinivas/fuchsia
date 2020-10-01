// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/developer/debug/shared/fd_watcher.h"
#include "src/developer/debug/shared/platform_message_loop.h"

#if defined(__Fuchsia__)
#include <lib/zx/socket.h>

#include "src/developer/debug/shared/socket_watcher.h"
#endif

namespace debug_ipc {

namespace {

// This helper class sets a boolean when it's destructed. Tests can use it to ensure that the
// lifetime of a lambda is correct.
class SetOnDestruct {
 public:
  // The flag must outlive this class.
  explicit SetOnDestruct(bool* flag) : flag_(flag) {}
  ~SetOnDestruct() { *flag_ = true; }

 private:
  bool* flag_;
};

}  // namespace

// This test either passes or hangs forever because the post didn't work.
// We could add a timer timeout, but if regular task posting doesn't work it's
// not clear why timer tasks would.
TEST(MessageLoop, PostQuit) {
  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  loop.PostTask(FROM_HERE, [loop_ptr = &loop]() { loop_ptr->QuitNow(); });
  loop.Run();

  loop.Cleanup();
}

// Like the above but expresses the task as a fit::promise.
TEST(MessageLoop, PostPendingTaskQuit) {
  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  loop.PostTask(FROM_HERE, fit::make_promise([&loop]() { loop.QuitNow(); }));
  loop.Run();

  loop.Cleanup();
}

TEST(MessageLoop, TimerQuit) {
  const uint64_t kNano = 1000000000;

  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  struct timespec start;
  ASSERT_FALSE(clock_gettime(CLOCK_MONOTONIC, &start));

  loop.PostTimer(FROM_HERE, 50, [loop_ptr = &loop]() { loop_ptr->QuitNow(); });
  loop.Run();

  struct timespec end;
  ASSERT_FALSE(clock_gettime(CLOCK_MONOTONIC, &end));
  ASSERT_GE(end.tv_sec, start.tv_sec);

  uint64_t nsec = (end.tv_sec - start.tv_sec) * kNano;
  nsec += end.tv_nsec;
  nsec -= start.tv_nsec;

  EXPECT_GE(nsec, 50u);

  // If we test an upper bound for nsec this test could potentially be flaky.
  // We don't actually make any guarantees about the upper bound anyway.

  loop.Cleanup();
}

// Tests a promise that suspends itself and then continues.
TEST(MessageLoop, SuspendPromise) {
  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  bool lambda_destructed = false;

  fit::suspended_task suspended;
  int run_count = 0;
  bool should_complete = false;
  loop.PostTask(FROM_HERE, fit::make_promise(
                               [&should_complete, &run_count, &suspended,
                                destructed = std::make_shared<SetOnDestruct>(&lambda_destructed)](
                                   fit::context& context) -> fit::result<> {
                                 run_count++;

                                 if (should_complete)
                                   return fit::ok();

                                 suspended = context.suspend_task();  // So we can signal later.
                                 return fit::pending();
                               }));

  // Should not have run yet.
  EXPECT_EQ(0, run_count);

  // Pulse the message loop. The task should have run once (responded with "suspend") and set the
  // suspended_task.
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(1, run_count);
  EXPECT_TRUE(suspended);

  // Run the loop again without doing anything. Nothing should have happened because the task was
  // not unsuspended.
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(1, run_count);

  // Mark the task as runnable again. It should run once but still report pending. Note that we
  // run the task synchronously from the resume_task() message as explained in
  // MessageLoop::resolve_ticket().
  suspended.resume_task();
  EXPECT_EQ(2, run_count);
  EXPECT_TRUE(suspended);
  EXPECT_FALSE(lambda_destructed);  // Lambda should not be deleted.

  // Tell the task to complete and signal again. It should be done.
  should_complete = true;
  suspended.resume_task();
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(3, run_count);
  EXPECT_FALSE(suspended);
  EXPECT_TRUE(lambda_destructed);  // Lambda should be deleted.

  loop.Cleanup();
}

// Duplicates the suspended_task controlling the suspended promise.
TEST(MessageLoop, DuplicateSuspendedPromise) {
  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  bool lambda_destructed = false;

  fit::suspended_task suspended;
  int run_count = 0;
  bool should_complete = false;
  loop.PostTask(FROM_HERE, fit::make_promise(
                               [&should_complete, &run_count, &suspended,
                                destructed = std::make_shared<SetOnDestruct>(&lambda_destructed)](
                                   fit::context& context) -> fit::result<> {
                                 run_count++;

                                 if (should_complete)
                                   return fit::ok();

                                 suspended = context.suspend_task();  // So we can signal later.
                                 return fit::pending();
                               }));

  // Should not have run yet.
  EXPECT_EQ(0, run_count);

  // Pulse the message loop. The task should have run once (responded with "suspend") and set the
  // suspended_task.
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(1, run_count);
  EXPECT_TRUE(suspended);

  // Duplicate the suspended task handle.
  fit::suspended_task suspended2 = suspended;
  should_complete = true;
  suspended.resume_task();  // Should run synchronously.
  EXPECT_EQ(2, run_count);

  // Resuming the other one does nothing. This suspend was already marked resolved from the other
  // token.
  should_complete = true;
  suspended2.resume_task();
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(2, run_count);  // Same as before.
  EXPECT_TRUE(lambda_destructed);

  loop.Cleanup();
}

// Tests a promise that suspends itself and then becomes abandoned (deleted before it's runnable).
TEST(MessageLoop, AbandonPromise) {
  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  bool lambda_destructed = false;

  fit::suspended_task suspended;
  int run_count = 0;
  loop.PostTask(FROM_HERE, fit::make_promise(
                               [&run_count, &suspended,
                                destructed = std::make_shared<SetOnDestruct>(&lambda_destructed)](
                                   fit::context& context) -> fit::result<> {
                                 run_count++;
                                 suspended = context.suspend_task();  // So we can signal later.
                                 return fit::pending();
                               }));

  // Should not have run yet.
  EXPECT_EQ(0, run_count);

  // Pulse the message loop. The task should have run once (responded with "suspend") and set the
  // suspended_task.
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(1, run_count);
  EXPECT_TRUE(suspended);

  // Free the suspended task. This should free the lambda and not run it.
  suspended = fit::suspended_task();
  EXPECT_EQ(1, run_count);
  EXPECT_TRUE(lambda_destructed);

  loop.Cleanup();
}

// Runs a promise right away without posting to the message loop.
TEST(MessageLoop, RunPromiseSync) {
  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  bool lambda_destructed = false;

  fit::suspended_task suspended;
  int run_count = 0;
  bool should_complete = false;
  loop.RunTask(FROM_HERE,
               fit::make_promise([&run_count, &suspended, &should_complete,
                                  destructed = std::make_shared<SetOnDestruct>(&lambda_destructed)](
                                     fit::context& context) -> fit::result<> {
                 run_count++;

                 if (should_complete)
                   return fit::ok();

                 suspended = context.suspend_task();  // So we can signal later.
                 return fit::pending();
               }));

  // Should have run but not completed.
  EXPECT_EQ(1, run_count);
  EXPECT_FALSE(lambda_destructed);
  EXPECT_TRUE(suspended);

  // Pulse the message loop.
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(1, run_count);  // Same as before.

  // Unsuspend, the task should complete.
  should_complete = true;
  suspended.resume_task();
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(2, run_count);
  EXPECT_FALSE(suspended);
  EXPECT_TRUE(lambda_destructed);

  loop.Cleanup();
}

// Runs a promise without posting from inside another promise.
TEST(MessageLoop, RunNestedPromiseSync) {
  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  fit::suspended_task inner_suspended;
  int inner_run_count = 0;
  bool inner_should_complete = false;

  fit::suspended_task outer_suspended;
  int outer_run_count = 0;
  bool outer_should_complete = false;

  loop.PostTask(FROM_HERE, fit::make_promise([&](fit::context& context) -> fit::result<> {
                  outer_run_count++;

                  if (outer_should_complete)
                    return fit::ok();

                  int old_inner_run_count = inner_run_count;
                  loop.RunTask(FROM_HERE,
                               fit::make_promise([&](fit::context& context) -> fit::result<> {
                                 inner_run_count++;

                                 if (inner_should_complete)
                                   return fit::ok();

                                 inner_suspended = context.suspend_task();
                                 return fit::pending();
                               }));
                  EXPECT_EQ(inner_run_count, old_inner_run_count + 1);  // Should have run once.

                  outer_suspended = context.suspend_task();  // So we can signal later.
                  return fit::pending();
                }));

  // Nothing should have happened yet.
  EXPECT_EQ(0, inner_run_count);
  EXPECT_EQ(0, outer_run_count);

  // Pulse the message loop. Both outer and inner loops should run once and suspend.
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(1, inner_run_count);
  EXPECT_EQ(1, outer_run_count);

  // Let the inner one complete.
  inner_should_complete = true;
  inner_suspended.resume_task();
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(2, inner_run_count);  // One more run.
  EXPECT_EQ(1, outer_run_count);  // Same as before.

  // Run the outer one again but it will still return async. This will queue up another inner loop
  // but this time the inner loop should exit right away and not set the inner suspended.
  inner_suspended = fit::suspended_task();
  outer_suspended.resume_task();
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(3, inner_run_count);  // One more run.
  EXPECT_EQ(2, outer_run_count);  // One more run.
  EXPECT_FALSE(inner_suspended);  // Not suspended, it completed synchronously now.

  // Complete the outer one.
  outer_should_complete = true;
  outer_suspended.resume_task();
  loop.PostTask(FROM_HERE, [&loop]() { loop.QuitNow(); });
  loop.Run();
  EXPECT_EQ(3, inner_run_count);  // Same as before
  EXPECT_EQ(3, outer_run_count);  // One more run.

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
    void OnFDReady(int fd, bool read, bool write, bool err) override {
      got_read = read;
      got_write = write;
      got_err = err;
      loop_->QuitNow();
    }

    bool got_read = false;
    bool got_write = true;
    bool got_err = true;

   private:
    MessageLoop* loop_;
  };

  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  // Scope everything to before MessageLoop::Cleanup().
  {
    ReadableWatcher watcher(&loop);

    // Going to write to pipefd[1] -> read from pipefd[0].
    MessageLoop::WatchHandle watch_handle =
        loop.WatchFD(MessageLoop::WatchMode::kRead, pipefd[0], &watcher);
    ASSERT_TRUE(watch_handle.watching());

    // Enqueue a task that should cause pipefd[0] to become readable.
    loop.PostTask(FROM_HERE, [write_fd = pipefd[1]]() { write(write_fd, "Hello", 5); });

    // This will quit on success because the OnFDReady callback called QuitNow,
    // or hang forever on failure.
    // TODO(brettw) add a timeout when timers are supported in the message loop.
    loop.Run();

    EXPECT_TRUE(watcher.got_read);
    EXPECT_FALSE(watcher.got_write);
    EXPECT_FALSE(watcher.got_err);
  }
  loop.Cleanup();
}

TEST(MessageLoop, RunUntilNoTasks) {
  PlatformMessageLoop loop;

  static constexpr int kCallCount = 5;
  struct Calls {
    int called[kCallCount];
  };

  Calls calls;
  for (int i = 0; i < kCallCount; i++) {
    calls.called[i] = -1;
  }

  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;
  {
    loop.PostTask(FROM_HERE, [&calls]() mutable { calls.called[0] = 0; });
    loop.PostTask(FROM_HERE, [&calls]() mutable { calls.called[1] = 1; });

    // Nested calles should work.
    loop.PostTask(FROM_HERE, [&calls, &loop]() mutable {
      loop.PostTask(FROM_HERE, [&calls, &loop]() {
        loop.PostTask(FROM_HERE, [&calls]() { calls.called[4] = 4; });
        calls.called[3] = 3;
      });
      calls.called[2] = 2;
    });

    loop.RunUntilNoTasks();

    // All should've been called in the expected order.
    EXPECT_EQ(calls.called[0], 0);
    EXPECT_EQ(calls.called[1], 1);
    EXPECT_EQ(calls.called[2], 2);
    EXPECT_EQ(calls.called[3], 3);
    EXPECT_EQ(calls.called[4], 4);
  }

  loop.Cleanup();
}

TEST(MessageLoop, RunUntilNoTasks_EmptyQueue) {
  PlatformMessageLoop loop;

  static constexpr int kCallCount = 5;
  struct Calls {
    int called[kCallCount];
  };

  Calls calls;
  for (int i = 0; i < kCallCount; i++) {
    calls.called[i] = -1;
  }

  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;
  { loop.RunUntilNoTasks(); }

  loop.Cleanup();
}

#if defined(__Fuchsia__)
TEST(MessageLoop, ZirconSocket) {
  zx::socket sender, receiver;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &sender, &receiver));

  class ReadableWatcher : public SocketWatcher {
   public:
    explicit ReadableWatcher(MessageLoop* loop) : loop_(loop) {}
    void OnSocketReadable(zx_handle_t socket_handle) override { loop_->QuitNow(); }

   private:
    MessageLoop* loop_;
  };

  PlatformMessageLoop loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  // Scope everything to before MessageLoop::Cleanup().
  {
    ReadableWatcher watcher(&loop);

    MessageLoop::WatchHandle watch_handle;
    ASSERT_EQ(ZX_OK, loop.WatchSocket(MessageLoop::WatchMode::kRead, receiver.get(), &watcher,
                                      &watch_handle));
    ASSERT_TRUE(watch_handle.watching());

    // Enqueue a task that should cause receiver to become readable.
    loop.PostTask(FROM_HERE, [&sender]() { sender.write(0, "Hello", 5, nullptr); });

    // This will quit on success because the OnSocketReadable callback called
    // QuitNow, or hang forever on failure.
    // TODO(brettw) add a timeout when timers are supported in the message loop.
    loop.Run();
  }
  loop.Cleanup();
}
#endif

}  // namespace debug_ipc
