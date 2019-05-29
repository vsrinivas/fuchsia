// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/job.h>
#include <lib/zx/object.h>
#include <zircon/status.h>

#include <array>
#include <string>
#include <thread>

#include "garnet/bin/sshd-host/service.h"
#include "gtest/gtest.h"

namespace sshd_host {
TEST(SshdHostTest, TestMakeChildJob) {
  zx_status_t s;
  zx::job parent;
  s = zx::job::create(*zx::job::default_job(), 0, &parent);
  ASSERT_EQ(s, ZX_OK);

  std::array<zx_koid_t, 10> children;
  size_t num_children;

  s = parent.get_info(ZX_INFO_JOB_CHILDREN, (void*)&children,
                      sizeof(zx_koid_t) * children.max_size(), &num_children,
                      nullptr);
  ASSERT_EQ(s, ZX_OK);

  ASSERT_EQ(num_children, (size_t)0);

  zx::job job;
  ASSERT_EQ(sshd_host::make_child_job(parent, std::string("test job"), &job), ZX_OK);

  s = parent.get_info(ZX_INFO_JOB_CHILDREN, (void*)&children,
                      sizeof(zx_koid_t) * children.size(), &num_children,
                      nullptr);
  ASSERT_EQ(s, ZX_OK);

  ASSERT_EQ(num_children, (size_t)1);

  zx_info_handle_basic_t info = {};

  s = job.get_info(ZX_INFO_HANDLE_BASIC, (void*)&info, sizeof(info),
                   nullptr, nullptr);
  ASSERT_EQ(s, ZX_OK);

  ASSERT_EQ(info.rights, kChildJobRights);
}
}  // namespace sshd_host
