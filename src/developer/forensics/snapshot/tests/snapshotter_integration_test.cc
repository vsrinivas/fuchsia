// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <gtest/gtest.h>

#include "src/developer/forensics/snapshot/snapshotter.h"
#include "src/developer/forensics/utils/archive.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"

namespace forensics {
namespace snapshot {
namespace {

class SnapshotterIntegrationTest : public testing::Test {
 public:
  void SetUp() override {
    environment_services_ = sys::ServiceDirectory::CreateFromNamespace();
    ASSERT_TRUE(tmp_dir_.NewTempFile(&snapshot_path_));
  }

 protected:
  std::shared_ptr<sys::ServiceDirectory> environment_services_;
  std::string snapshot_path_;

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(SnapshotterIntegrationTest, SmokeTest) {
  ASSERT_TRUE(MakeSnapshot(environment_services_, snapshot_path_.data()));

  // We simply assert that we can unpack the snapshot archive.
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromFilename(snapshot_path_, &vmo));
  fuchsia::mem::Buffer buffer = std::move(vmo).ToTransport();
  std::map<std::string, std::string> unpacked_attachments;
  ASSERT_TRUE(Unpack(buffer, &unpacked_attachments));
}

}  // namespace
}  // namespace snapshot
}  // namespace forensics
