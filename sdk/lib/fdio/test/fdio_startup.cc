// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/limits.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <fbl/auto_call.h>
#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

namespace {

// Ensure that the child crashes when given bogus fd handle info.
template <uint32_t... Info>
void BadFdTest() {
  const char* const argv[] = {"/pkg/bin/hello-world", nullptr};

  zx::channel bs_parent, bs_child;
  ASSERT_OK(zx::channel::create(0, &bs_parent, &bs_child));

  struct Message {
    zx_proc_args_t header;
    uint32_t handle_info[sizeof...(Info)];
  } msg{};
  zx_handle_t handles[sizeof...(Info)];

  msg.header.protocol = ZX_PROCARGS_PROTOCOL;
  msg.header.version = ZX_PROCARGS_VERSION;
  msg.header.handle_info_off = offsetof(Message, handle_info);

  zx_handle_t* h = handles;
  uint32_t* i = msg.handle_info;
  for (uint32_t info : {Info...}) {
    zx::channel c0, c1;
    ASSERT_OK(zx::channel::create(0, &c0, &c1));
    *h++ = c1.release();
    *i++ = info;
  }

  ASSERT_OK(bs_parent.write(0, &msg, sizeof(msg), handles, sizeof...(Info)));

  auto sb = tu_launch_init(ZX_HANDLE_INVALID, nullptr, 1, argv, 0, nullptr, 0, nullptr, nullptr);

  springboard_set_bootstrap(sb, bs_child.release());

  zx::process proc(tu_launch_fini(sb));
  zx_signals_t signals;
  ASSERT_OK(proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), &signals));
  ASSERT_TRUE(signals & ZX_PROCESS_TERMINATED);

  zx_info_process_t pinfo;
  ASSERT_OK(proc.get_info(ZX_INFO_PROCESS, &pinfo, sizeof(pinfo), nullptr, nullptr));

  EXPECT_NE(pinfo.return_code, 0);
}

TEST(Startup, InvalidFd) { BadFdTest<PA_HND(PA_FD, FDIO_MAX_FD)>(); }

TEST(Startup, DuplicateFd) { BadFdTest<PA_HND(PA_FD, 0), PA_HND(PA_FD, 0)>(); }

}  // namespace
