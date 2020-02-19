// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/minfs/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "minfs_fixtures.h"
#include "utils.h"

namespace {

using fs::FilesystemTest;

using MountStateTest = MinfsTest;
using MountStateTestWithFvm = MinfsTestWithFvm;

TEST_F(MountStateTest, ReadWriteWithJournal) {
  fbl::unique_fd fd(open(mount_path(), O_DIRECTORY | O_RDONLY));

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = ::llcpp::fuchsia::minfs::Minfs::Call::GetMountState(caller.channel());
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result.value().status);
  ASSERT_NOT_NULL(result.value().mount_state);

  ASSERT_EQ(result.value().mount_state->repair_filesystem, true);
  ASSERT_EQ(result.value().mount_state->readonly_after_initialization, false);
  ASSERT_EQ(result.value().mount_state->collect_metrics, false);
  ASSERT_EQ(result.value().mount_state->verbose, false);
  ASSERT_EQ(result.value().mount_state->use_journal, true);
}

TEST_F(MountStateTestWithFvm, ReadWriteWithJournal) {
  fbl::unique_fd fd(open(mount_path(), O_DIRECTORY | O_RDONLY));

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = ::llcpp::fuchsia::minfs::Minfs::Call::GetMountState(caller.channel());
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result.value().status);
  ASSERT_NOT_NULL(result.value().mount_state);

  ASSERT_EQ(result.value().mount_state->repair_filesystem, true);
  ASSERT_EQ(result.value().mount_state->readonly_after_initialization, false);
  ASSERT_EQ(result.value().mount_state->collect_metrics, false);
  ASSERT_EQ(result.value().mount_state->verbose, false);
  ASSERT_EQ(result.value().mount_state->use_journal, true);
}

}  // namespace
