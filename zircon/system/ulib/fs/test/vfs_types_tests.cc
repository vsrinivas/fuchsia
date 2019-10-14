// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs_types.h>
#include <unittest/unittest.h>

namespace {

bool ReadOnlyRights() {
  BEGIN_TEST;

  // clang-format off
  EXPECT_TRUE (fs::Rights::ReadOnly().read,    "Bad value for Rights::ReadOnly().read");
  EXPECT_FALSE(fs::Rights::ReadOnly().write,   "Bad value for Rights::ReadOnly().write");
  EXPECT_FALSE(fs::Rights::ReadOnly().admin,   "Bad value for Rights::ReadOnly().admin");
  EXPECT_FALSE(fs::Rights::ReadOnly().execute, "Bad value for Rights::ReadOnly().execute");
  // clang-format on

  END_TEST;
}

bool WriteOnlyRights() {
  BEGIN_TEST;

  // clang-format off
  EXPECT_FALSE(fs::Rights::WriteOnly().read,    "Bad value for Rights::WriteOnly().read");
  EXPECT_TRUE (fs::Rights::WriteOnly().write,   "Bad value for Rights::WriteOnly().write");
  EXPECT_FALSE(fs::Rights::WriteOnly().admin,   "Bad value for Rights::WriteOnly().admin");
  EXPECT_FALSE(fs::Rights::WriteOnly().execute, "Bad value for Rights::WriteOnly().execute");
  // clang-format on

  END_TEST;
}

bool ReadWriteRights() {
  BEGIN_TEST;

  // clang-format off
  EXPECT_TRUE (fs::Rights::ReadWrite().read,    "Bad value for Rights::ReadWrite().read");
  EXPECT_TRUE (fs::Rights::ReadWrite().write,   "Bad value for Rights::ReadWrite().write");
  EXPECT_FALSE(fs::Rights::ReadWrite().admin,   "Bad value for Rights::ReadWrite().admin");
  EXPECT_FALSE(fs::Rights::ReadWrite().execute, "Bad value for Rights::ReadWrite().execute");
  // clang-format on

  END_TEST;
}

bool ReadExecRights() {
  BEGIN_TEST;

  // clang-format off
  EXPECT_TRUE (fs::Rights::ReadExec().read,    "Bad value for Rights::ReadExec().read");
  EXPECT_FALSE(fs::Rights::ReadExec().write,   "Bad value for Rights::ReadExec().write");
  EXPECT_FALSE(fs::Rights::ReadExec().admin,   "Bad value for Rights::ReadExec().admin");
  EXPECT_TRUE (fs::Rights::ReadExec().execute, "Bad value for Rights::ReadExec().execute");
  // clang-format on

  END_TEST;
}

bool AllRights() {
  BEGIN_TEST;

  // clang-format off
  EXPECT_TRUE (fs::Rights::All().read,    "Bad value for Rights::All().read");
  EXPECT_TRUE (fs::Rights::All().write,   "Bad value for Rights::All().write");
  EXPECT_TRUE (fs::Rights::All().admin,   "Bad value for Rights::All().admin");
  EXPECT_TRUE (fs::Rights::All().execute, "Bad value for Rights::All().execute");
  // clang-format on

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(vfs_types_tests)
RUN_TEST(ReadOnlyRights)
RUN_TEST(WriteOnlyRights)
RUN_TEST(ReadWriteRights)
RUN_TEST(ReadExecRights)
RUN_TEST(AllRights)
END_TEST_CASE(vfs_types_tests)
