// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_
#define SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_

#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fdio/directory.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace devmgr {

class FshostIntegrationTest : public testing::Test {
 public:
  void SetUp() override;

  const fidl::SynchronousInterfacePtr<fuchsia::io::Directory>& exposed_dir() const {
    return exposed_dir_;
  }

  const zx::channel& watcher_channel() const { return watcher_channel_; }

  void PauseWatcher() const;
  void ResumeWatcher() const;

  // Waits for a mount to happen at the given toplevel path. The mounted filesystem is expected to
  // have the given type (VFS_TYPE_MINFS, etc.).
  //
  // Times out after 10s.
  fbl::unique_fd WaitForMount(const std::string& name, uint64_t expected_fs_type);

 private:
  fidl::SynchronousInterfacePtr<fuchsia::io::Directory> exposed_dir_;
  zx::channel watcher_channel_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_H_
