// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/appmgr.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <lib/async-loop/cpp/loop.h>
#include <zx/channel.h>

#include "gtest/gtest.h"

namespace fuchsia {
namespace sys {
namespace {

TEST(Appmgr, RunUntilIdle) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  AppmgrArgs args{.pa_directory_request = ZX_HANDLE_INVALID,
                  .sysmgr_url = "sysmgr",
                  .sysmgr_args = {},
                  .run_virtual_console = false,
                  .retry_sysmgr_crash = false};
  Appmgr appmgr(loop.async(), std::move(args));
  EXPECT_FALSE(loop.RunUntilIdle());
}

}  // namespace
}  // namespace sys
}  // namespace fuchsia
