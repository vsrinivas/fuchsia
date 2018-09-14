// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <zx/event.h>
#include <zx/process.h>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/util.h"
#include "gtest/gtest.h"

namespace debugger_utils {
namespace {

TEST(UtilZx, GetKoid) {
  zx::event event1, event2;
  EXPECT_EQ(ZX_OK, zx::event::create(0u, &event1));
  EXPECT_EQ(ZX_OK, event1.duplicate(ZX_RIGHT_SAME_RIGHTS, &event2));
  EXPECT_NE(ZX_KOID_INVALID, GetKoid(event1));
  EXPECT_EQ(GetKoid(event1), GetKoid(event2));
  EXPECT_NE(ZX_KOID_INVALID, GetKoid(event1.get()));
  EXPECT_EQ(GetKoid(event1.get()), GetKoid(event2.get()));
}

TEST(UtilZx, GetRelatedKoid) {
  // The "related" koid of a process is its immediate parent job.
  // Note we don't exercise all possible objects here. Doing so is the job
  // of a kernel unittest. This test just exercises GetRelatedKoid().
  zx::process process;
  zx::vmar root_vmar;
  zx::job job{GetDefaultJob()};
  char name[] = "GetRelatedKoid process";
  EXPECT_EQ(ZX_OK, zx::process::create(job, name, sizeof(name),
                                       0u, &process, &root_vmar));
  EXPECT_NE(ZX_KOID_INVALID, GetRelatedKoid(process));
  EXPECT_EQ(GetRelatedKoid(process), GetKoid(job));
  EXPECT_NE(ZX_KOID_INVALID, GetRelatedKoid(process.get()));
  EXPECT_EQ(GetRelatedKoid(process.get()), GetKoid(job.get()));
}

}  // namespace
}  // namespace debugger_utils
