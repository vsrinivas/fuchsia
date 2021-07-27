// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_BLOBFS_TEST_SETUP_H_
#define SRC_STORAGE_BLOBFS_TEST_BLOBFS_TEST_SETUP_H_

#include <memory>

#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/common.h"

namespace blobfs {

constexpr FilesystemOptions DefaultFilesystemOptions() {
  return FilesystemOptions{
      .num_inodes = 512,
  };
}

// Provides the base Blobfs setup without providing a message loop. See the variants below.
//
// Blobfs shutdown is tricky. The message loop must process any pending messages (so the vmo
// free notifications can be delivered and the Blobs can unregister themselves first), then the
// Blobfs instance must be deleted, then the Vfs instance must be deleted. This must happen in the
// derived class' destructors so the loop gets destroyed last.
class BlobfsTestSetupBase {
 public:
  async::Loop& loop() { return GetLoop(); }
  async_dispatcher_t* dispatcher() { return GetLoop().dispatcher(); }

  // These pointers will be null when not mounted.
  fs::PagedVfs* vfs() { return vfs_.get(); }
  Blobfs* blobfs() { return blobfs_.get(); }

  // Creates a fake block device, formats it with the given options, and mounts it.
  zx_status_t CreateFormatMount(uint64_t block_count, uint32_t block_size,
                                const FilesystemOptions& fs_options = DefaultFilesystemOptions(),
                                const MountOptions& mount_options = MountOptions());

  zx_status_t Mount(std::unique_ptr<BlockDevice> device,
                    const MountOptions& options = MountOptions());
  std::unique_ptr<BlockDevice> Unmount();

  // Unmounts and remounts using the given options.
  //
  // Any Blob references that the test holds will need to be released before this function is
  // called or the BlobCache destructor will assert that there are live blobs.
  zx_status_t Remount(const MountOptions& options = MountOptions());

 protected:
  // Provided by the derived classes that set up the loop.
  virtual async::Loop& GetLoop() = 0;

  virtual void ShutdownVfs() = 0;

  // Should be called in the derived class' destructor.
  void DestroyBlobfs();

  std::unique_ptr<fs::PagedVfs> vfs_;
  std::unique_ptr<Blobfs> blobfs_;
};

// A test class that will set up a message loop, vfs, and blobfs instance. The message loop will run
// on the current thread. This simplifies access to the Blobfs class and allows most types of tests,
// but will not support fidl calls.
class BlobfsTestSetup : public BlobfsTestSetupBase {
 public:
  BlobfsTestSetup() = default;
  ~BlobfsTestSetup();

 private:
  async::Loop& GetLoop() override { return loop_; }
  void ShutdownVfs() override;

  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};
};

// Like BlobfsTestSetup but runs the Blobfs on a background thread. The test must ensure that
// access to the Blobfs object happens on only one thread at a time. But this allows fidl calls
// to be made that are not possible when running on only one thread.
class BlobfsTestSetupWithThread : public BlobfsTestSetupBase {
 public:
  BlobfsTestSetupWithThread();
  ~BlobfsTestSetupWithThread();

 private:
  async::Loop& GetLoop() override { return loop_; }
  void ShutdownVfs() override;

  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_BLOBFS_TEST_SETUP_H_
