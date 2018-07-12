// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/zx/channel.h>

#include "garnet/lib/inferior_control/process.h"
#include "garnet/lib/inferior_control/test_server.h"

#include "gtest/gtest.h"

namespace debugserver {
namespace {

// TODO(dje): Obtain path more cleanly.
const char helper_program[] = "/system/test/helper/inferior_control_test_helper";

using ProcessTest = TestServer;

TEST_F(ProcessTest, Launch) {
  std::vector<std::string> argv{
    helper_program,
  };
  ASSERT_TRUE(SetupInferior(argv));

  EXPECT_TRUE(RunHelperProgram(zx::channel()));
  EXPECT_TRUE(Run());
  EXPECT_TRUE(TestSuccessfulExit());
}

// Test detaching and re-attaching.
// To add some determinism, we wait for the main thread to finish starting
// before detaching. This ensures we always have processed the main
// thread's ZX_EXCP_THREAD_STARTING exception before detaching.
// Note: Exceptions are handled in the same thread as server.Run().

class AttachTest : public TestServer {
 public:
  AttachTest() = default;

  void OnThreadStarting(Process* process, Thread* thread,
                        const zx_exception_context_t& context) override {
    if (!main_thread_started_) {
      // Must be the inferior's main thread.
      main_thread_started_ = true;
      async::PostTask(message_loop().dispatcher(), [this] { DoDetachAttach(); });
    }
    TestServer::OnThreadStarting(process, thread, context);
  }

  void DoDetachAttach() {
    auto inferior = current_process();
    auto pid = inferior->id();
    EXPECT_TRUE(inferior->Detach());
    EXPECT_TRUE(inferior->Attach(pid));
    // If attaching failed we'll hang since we won't see the inferior exiting.
    if (!inferior->IsAttached()) {
      QuitMessageLoop(true);
    }
    // The inferior is waiting for us to close our side of the channel.
    channel_.reset();
  }

  void set_channel(zx::channel channel) { channel_ = std::move(channel); }

 private:
  bool main_thread_started_ = false;
  zx::channel channel_;
};

TEST_F(AttachTest, Attach) {
  std::vector<std::string> argv{
    helper_program,
    "test-attach",
  };
  ASSERT_TRUE(SetupInferior(argv));

  zx::channel our_channel, their_channel;
  auto status = zx::channel::create(0, &our_channel, &their_channel);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_TRUE(RunHelperProgram(std::move(their_channel)));
  set_channel(std::move(our_channel));

  EXPECT_TRUE(Run());
  EXPECT_TRUE(TestSuccessfulExit());
}

}  // namespace
}  // namespace debugserver
