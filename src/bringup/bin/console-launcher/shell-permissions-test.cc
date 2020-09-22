// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <zxtest/zxtest.h>

// Exercise ZX_POL_AMBIENT_MARK_VMO_EXEC.
// This should only succeed in the Zircon shell which should have this priviledge.
TEST(ShellPermissionsTest, ShellJobHasAmbientVmex) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(1, 0, &vmo), ZX_OK);
  ASSERT_EQ(vmo.replace_as_executable(zx::resource(), &vmo), ZX_OK);
}

// Exercise ZX_POL_NEW_PROCESS.
// This should only succeed in the Zircon shell which should have this priviledge.
TEST(ShellPermissionsTest, ShellJobHasNewProcess) {
  zx::process proc;
  zx::vmar root_vmar;
  const char name[] = "foo";
  ASSERT_EQ(zx::process::create(*zx::job::default_job(), name, strlen(name), 0, &proc, &root_vmar),
            ZX_OK);
}
