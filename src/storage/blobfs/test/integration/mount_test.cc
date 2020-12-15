// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/mount.h"

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/zx/resource.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/rights.h>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/runner.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"
#include "src/storage/blobfs/test/integration/fdio_test.h"

namespace blobfs {
namespace {

namespace fio = ::llcpp::fuchsia::io;

// Uses the default layout of kDataRootOnly.
using DataMountTest = BlobfsTest;

// Variant that sets the layout to kExportDirectory.
class OutgoingMountTest : public FdioTest {
 public:
  OutgoingMountTest() { set_layout(ServeLayout::kExportDirectory); }
};

// merkle root for a file. in order to create a file on blobfs we need the filename to be a valid
// merkle root whether or not we ever write the content.
//
// This is valid enough to create files but it is unknown what content this was generated
// from. Previously this comment said it was "test content" but that seems to be incorrect.
constexpr std::string_view kFileName =
    "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";

TEST_F(DataMountTest, DataRootHasNoRootDirectoryInIt) {
  errno = 0;
  fbl::unique_fd no_fd(openat(root_fd(), kOutgoingDataRoot, O_RDONLY));
  ASSERT_FALSE(no_fd.is_valid());
  ASSERT_EQ(errno, EINVAL);
}

TEST_F(DataMountTest, DataRootCanHaveBlobsCreated) {
  fbl::unique_fd foo_fd(openat(root_fd(), kFileName.data(), O_CREAT));
  ASSERT_TRUE(foo_fd.is_valid());
}

TEST_F(OutgoingMountTest, OutgoingDirectoryHasRootDirectoryInIt) {
  fbl::unique_fd no_fd(openat(root_fd(), kOutgoingDataRoot, O_DIRECTORY));
  ASSERT_TRUE(no_fd.is_valid());
}

TEST_F(OutgoingMountTest, OutgoingDirectoryIsReadOnly) {
  fbl::unique_fd foo_fd(openat(root_fd(), kFileName.data(), O_CREAT));
  ASSERT_FALSE(foo_fd.is_valid());
}

TEST_F(OutgoingMountTest, OutgoingDirectoryDataRootCanHaveBlobsCreated) {
  std::string path = std::string("root/") + kFileName.data();
  fbl::unique_fd foo_fd(openat(root_fd(), path.c_str(), O_CREAT));
  ASSERT_TRUE(foo_fd.is_valid());
}

// Verify that if no valid Resource of at least KIND_VMEX is provided to the filesystem during
// creation then it does not support VMO_FLAG_EXEC to obtain executable VMOs from blobs.
TEST_F(DataMountTest, CannotLoadBlobsExecutable) {
  // Create a new blob with random contents on the mounted filesystem.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(".", 1 << 16, &info));

  fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd.is_valid());

  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
  ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
      << "Failed to write Data";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  fd.reset();

  // Open the new blob again but with READABLE | EXECUTABLE rights, then confirm that we can get the
  // blob contents as a VMO but not as an executable VMO.
  ASSERT_EQ(
      fdio_open_fd_at(root_fd(), info->path, fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                      fd.reset_and_get_address()),
      ZX_OK);
  ASSERT_TRUE(fd.is_valid());

  zx::vmo vmo;
  ASSERT_EQ(fdio_get_vmo_clone(fd.get(), vmo.reset_and_get_address()), ZX_OK);
  ASSERT_TRUE(vmo.is_valid());

  vmo.reset();
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_get_vmo_exec(fd.get(), vmo.reset_and_get_address()));
  ASSERT_FALSE(vmo.is_valid());
}

}  // namespace
}  // namespace blobfs
