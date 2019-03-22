// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>
#include <lib/zx/socket.h>

#include <lib/fdio/spawn.h>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/test_helper.h"
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
  zx::job job{GetDefaultJob()};

  static const char* const argv[] = {
    kTestHelperPath,
    nullptr,
  };

  zx_status_t status = fdio_spawn(job.get(), FDIO_SPAWN_CLONE_ALL,
                                  kTestHelperPath, argv,
                                  process.reset_and_get_address());
  EXPECT_EQ(status, ZX_OK);

  EXPECT_NE(ZX_KOID_INVALID, GetRelatedKoid(process));
  EXPECT_EQ(GetRelatedKoid(process), GetKoid(job));
  EXPECT_NE(ZX_KOID_INVALID, GetRelatedKoid(process.get()));
  EXPECT_EQ(GetRelatedKoid(process.get()), GetKoid(job.get()));
}

TEST(UtilZx, GetObjectName) {
  zx_handle_t self = zx_thread_self();
  static const char name[] = "GetObjectNameTest";
  ASSERT_EQ(zx_object_set_property(self, ZX_PROP_NAME, name, sizeof(name)),
            ZX_OK);
  ASSERT_STREQ(GetObjectName(self).c_str(), name);
}

TEST(UtilZx, GetNoNameObjectName) {
  // Events don't have names, but also don't have properties,
  // so we'll get ACCESS_DENIED.
  zx::event event;
  EXPECT_EQ(zx::event::create(0u, &event), ZX_OK);
  static const char name[] = "GetNoNameObjectNameTest";
  ASSERT_EQ(event.set_property(ZX_PROP_NAME, name, sizeof(name)),
            ZX_ERR_ACCESS_DENIED);
  ASSERT_STREQ(GetObjectName(event).c_str(), "");

  // Sockets have properties but not names, so we'll get NOT_SUPPORTED.
  zx::socket socket0, socket1;
  EXPECT_EQ(zx::socket::create(0u, &socket0, &socket1), ZX_OK);
  ASSERT_EQ(socket0.set_property(ZX_PROP_NAME, name, sizeof(name)),
            ZX_ERR_NOT_SUPPORTED);
  ASSERT_STREQ(GetObjectName(socket0).c_str(), "");
}

}  // namespace
}  // namespace debugger_utils
