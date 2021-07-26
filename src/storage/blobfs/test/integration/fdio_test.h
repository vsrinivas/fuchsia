// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_INTEGRATION_FDIO_TEST_H_
#define SRC_STORAGE_BLOBFS_TEST_INTEGRATION_FDIO_TEST_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/cpp/hierarchy.h>

#include <block-client/cpp/fake-device.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/blobfs/runner.h"

namespace blobfs {

// Test harness that sets up a blobfs and fdio backed by a FakeBlockDevice.
class FdioTest : public testing::Test {
 public:
  FdioTest() = default;

  void SetUp() override;
  void TearDown() override;

 protected:
  async::Loop* loop() { return loop_.get(); }

  int export_root_fd() const { return export_root_fd_.get(); }
  // get a clone of the export root in handle form
  zx_handle_t export_root();
  int root_fd() const { return root_fd_.get(); }
  block_client::FakeBlockDevice* block_device() { return block_device_; }

  void set_mount_options(MountOptions options) { mount_options_ = options; }

  // The vmex resource defaults to empty. It only needs to be set if a test requires it.
  void set_vmex_resource(zx::resource resource) { vmex_resource_ = std::move(resource); }

  // Fetches a fresh Inspect snapshot from the running blobfs instance.
  fpromise::result<inspect::Hierarchy> TakeSnapshot();

  // Takes an inspect snapshot `hierarchy` and navigates through the nodes using
  // the `path` given and fetches the `property` there to be stored in `value`.
  static void GetUintMetricFromHierarchy(const inspect::Hierarchy& hierarchy,
                                         const std::vector<std::string>& path,
                                         const std::string& property, uint64_t* value);

  // Strings together `TakeSnapshot` and `GetUintMetricFromHierarchy` to fetch a
  // single value from a fresh snapshot.
  void GetUintMetric(const std::vector<std::string>& path, const std::string& property,
                     uint64_t* value);

  virtual uint64_t GetOldestMinorVersion() const { return kBlobfsCurrentMinorVersion; }

  virtual BlobLayoutFormat GetBlobLayoutFormat() const {
    return BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart;
  }

 private:
  block_client::FakeBlockDevice* block_device_ = nullptr;  // Owned by the runner_.

  MountOptions mount_options_;
  zx::resource vmex_resource_;
  fbl::unique_fd export_root_fd_;
  fbl::unique_fd root_fd_;
  std::unique_ptr<Runner> runner_;

  std::unique_ptr<async::Loop> loop_;  // Must be destroyed after the runner.
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_INTEGRATION_FDIO_TEST_H_
