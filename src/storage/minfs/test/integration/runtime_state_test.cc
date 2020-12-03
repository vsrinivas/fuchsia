// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/minfs/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/minfs_test.h"

namespace minfs {
namespace {

using MountStateTest = fs_test::FilesystemTest;

TEST_P(MountStateTest, ReadWriteWithJournal) {
  fbl::unique_fd fd(open(fs().mount_path().c_str(), O_DIRECTORY | O_RDONLY));

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = ::llcpp::fuchsia::minfs::Minfs::Call::GetMountState(caller.channel());
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value().status, ZX_OK);
  ASSERT_NE(result.value().mount_state, nullptr);

  ASSERT_EQ(result.value().mount_state->repair_filesystem, true);
  ASSERT_EQ(result.value().mount_state->readonly_after_initialization, false);
  ASSERT_EQ(result.value().mount_state->collect_metrics, false);
  ASSERT_EQ(result.value().mount_state->verbose, false);
  ASSERT_EQ(result.value().mount_state->use_journal, true);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, MountStateTest, testing::ValuesIn(fs_test::AllTestMinfs()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace minfs
