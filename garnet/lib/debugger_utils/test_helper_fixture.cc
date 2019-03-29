// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <src/lib/fxl/arraysize.h>
#include <src/lib/fxl/logging.h>
#include <zircon/processargs.h>

#include "test_helper.h"
#include "test_helper_fixture.h"
#include "util.h"

namespace debugger_utils {

const char* const TestWithHelper::kWaitPeerClosedArgv[] = {
  kTestHelperPath,
  "wait-peer-closed",
  nullptr,
};

void TestWithHelper::SetUp() {
}

void TestWithHelper::TearDown() {
  // Closing the channel should cause the helper to terminate, if it
  // hasn't already.
  channel_.reset();

  zx_signals_t pending;
  zx_status_t status =
    process_.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), &pending);
  ASSERT_EQ(status, ZX_OK);
}

zx_status_t TestWithHelper::RunHelperProgram(const zx::job& job,
                                             const char* const argv[]) {
  zx::channel our_channel, their_channel;
  zx_status_t status = zx::channel::create(0, &our_channel, &their_channel);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::channel::create failed: " << ZxErrorString(status);
    return status;
  }
  fdio_spawn_action actions[1];
  actions[0].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
  actions[0].h.id = PA_HND(PA_USER0, 0);
  actions[0].h.handle = their_channel.release();
  uint32_t flags = FDIO_SPAWN_CLONE_ALL;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];

  zx::process process;
  status = fdio_spawn_etc(job.get(), flags, kTestHelperPath, argv, nullptr,
                          arraysize(actions), actions,
                          process.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "fdio_spawn_etc failed: " << ZxErrorString(status)
                   << ", " << err_msg;
    return status;
  }

  process_ = std::move(process);
  channel_ = std::move(our_channel);
  return status;
}

zx_status_t TestWithHelper::GetHelperThread(zx::thread* out_thread) {
  zx_signals_t pending;
  zx_status_t status =
    channel_.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &pending);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "channel->wait_one failed: " << ZxErrorString(status);
    return status;
  }

  uint32_t actual_bytes, actual_handles;
  status = channel_.read(0u, nullptr, 0u,
                         &actual_bytes, out_thread->reset_and_get_address(),
                         1u, &actual_handles);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "channel->read failed: " << ZxErrorString(status);
    return status;
  }
  EXPECT_EQ(actual_bytes, 0u);
  EXPECT_EQ(actual_handles, 1u);

  // At this point the inferior is generally waiting for us to close the
  // channel.
  return ZX_OK;
}

}  // namespace debugger_utils
