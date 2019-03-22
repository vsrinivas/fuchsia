// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_TEST_HELPER_FIXTURE_H_
#define GARNET_LIB_DEBUGGER_UTILS_TEST_HELPER_FIXTURE_H_

#include <gtest/gtest.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace debugger_utils {

class TestWithHelper : public ::testing::Test {
 public:
  // Pass this for |argv| to have the inferior send back a handle to its
  // main thread and then wait for us to close the channel.
  static const char* const kWaitPeerClosedArgv[];

  // ::testing::Test overrides
  void SetUp() override;
  void TearDown() override;

  // Call this to run the helper program with |argv| under |job|.
  zx_status_t RunHelperProgram(const zx::job& job, const char* const argv[]);

  // Call this after |RunHelperProgram| to obtain the handle of the main
  // thread in the helper program. This assumes the helper program is
  // following the necessary protocol to send the handle.
  zx_status_t GetHelperThread(zx::thread* out_thread);

  const zx::process& process() const { return process_; }
  const zx::channel& channel() const { return channel_; }

 private:
  zx::process process_;
  zx::channel channel_;
};

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_TEST_HELPER_FIXTURE_H_
