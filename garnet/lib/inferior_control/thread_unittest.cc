// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/zx/channel.h>

#include "garnet/lib/inferior_control/process.h"
#include "garnet/lib/inferior_control/test_server.h"

#include "gtest/gtest.h"

namespace inferior_control {
namespace {

// TODO(dje): Obtain path more cleanly.
const char helper_program[] =
    "/pkgfs/packages/inferior_control_tests/0/bin/"
    "inferior_control_test_helper";

// Test resume from exception and try-next.
// Note: Exceptions are handled in the same thread as server.Run().

class ThreadTest : public TestServer {
 public:
  ThreadTest() = default;

  bool got_sw_breakpoint() const { return got_sw_breakpoint_; }
  bool got_unexpected_exception() const { return got_unexpected_exception_; }

  void OnArchitecturalException(
      Process* process, Thread* thread, const zx_excp_type_t type,
      const zx_exception_context_t& context) {
    FXL_LOG(INFO) << "Got exception 0x" << std::hex << type;
    if (type == ZX_EXCP_SW_BREAKPOINT) {
      got_sw_breakpoint_ = true;
      thread->TryNext();
    } else {
      // We shouldn't get here, test has failed.
      // Record the fact for the test, and terminate the inferior, we don't
      // want the exception propagating to the system exception handler.
      got_unexpected_exception_ = true;
      zx_task_kill(process->handle());
    }
  }

 private:
  bool got_sw_breakpoint_ = false;
  bool got_unexpected_exception_ = false;
};

TEST_F(ThreadTest, ResumeTryNextTest) {
  std::vector<std::string> argv{
    helper_program,
    "test-try-next",
  };
  ASSERT_TRUE(SetupInferior(argv));

  zx::channel our_channel, their_channel;
  auto status = zx::channel::create(0, &our_channel, &their_channel);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_TRUE(RunHelperProgram(std::move(their_channel)));

  // The inferior is waiting for us to close our side of the channel.
  our_channel.reset();

  EXPECT_TRUE(Run());
  EXPECT_TRUE(TestSuccessfulExit());
  EXPECT_TRUE(got_sw_breakpoint());
  EXPECT_FALSE(got_unexpected_exception());
}

}  // namespace
}  // namespace inferior_control
