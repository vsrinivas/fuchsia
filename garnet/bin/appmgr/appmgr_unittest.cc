// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/appmgr.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/channel.h>

namespace component {
namespace {

using AppmgrTest = ::gtest::RealLoopFixture;

TEST_F(AppmgrTest, RunUntilIdle) {
  auto services = std::make_shared<sys::ServiceDirectory>();
  AppmgrArgs args{
      .pa_directory_request = ZX_HANDLE_INVALID,
      .environment_services = services,
      .sysmgr_url = "fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx",
      .sysmgr_args = {},
      .run_virtual_console = false,
      .retry_sysmgr_crash = false};
  Appmgr appmgr(dispatcher(), std::move(args));
  bool called;
  async::PostTask(dispatcher(), [&called] { called = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace component
