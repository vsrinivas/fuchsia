// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_
#define SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_

#include <fidl/fuchsia.component/cpp/wire.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <lib/fdio/directory.h>

#include <string_view>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace fshost {

class FshostIntegrationTest : public testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  const fidl::WireSyncClient<fuchsia_io::Directory>& exposed_dir() const { return exposed_dir_; }

  const fidl::WireSyncClient<fuchsia_fshost::BlockWatcher>& block_watcher() const {
    return block_watcher_;
  }

  void PauseWatcher() const;
  void ResumeWatcher() const;

  // Waits for a mount to happen at the given toplevel path. If successful returns a file
  // descriptor opened on the root and the filesystem type (VFS_TYPE_MINFS, etc.).
  //
  // Times out after 10s.
  std::pair<fbl::unique_fd, uint64_t> WaitForMount(const std::string& name);

 private:
  fidl::WireSyncClient<fuchsia_io::Directory> exposed_dir_;
  fidl::WireSyncClient<fuchsia_component::Realm> realm_;
  fidl::WireSyncClient<fuchsia_fshost::BlockWatcher> block_watcher_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_
