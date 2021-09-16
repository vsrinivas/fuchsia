// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_
#define SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_

#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fdio/directory.h>

#include <string_view>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace fshost {

class FshostIntegrationTest : public testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  const fidl::SynchronousInterfacePtr<fuchsia::io::Directory>& exposed_dir() const {
    return exposed_dir_;
  }

  const zx::channel& watcher_channel() const { return watcher_channel_; }

  void PauseWatcher() const;
  void ResumeWatcher() const;

  // Waits for a mount to happen at the given toplevel path. If successful returns a file
  // descriptor opened on the root and the filesystem type (VFS_TYPE_MINFS, etc.).
  //
  // Times out after 10s.
  std::pair<fbl::unique_fd, uint64_t> WaitForMount(const std::string& name);

 private:
  fidl::SynchronousInterfacePtr<fuchsia::io::Directory> exposed_dir_;
  fidl::SynchronousInterfacePtr<fuchsia::sys2::Realm> realm_;
  zx::channel watcher_channel_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_
