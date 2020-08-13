// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_INTEGRATION_LOAD_GENERATOR_H_
#define SRC_STORAGE_BLOBFS_TEST_INTEGRATION_LOAD_GENERATOR_H_

#include <lib/zircon-internal/thread_annotations.h>

#include <array>
#include <list>
#include <memory>

#include <fbl/mutex.h>
#include <fbl/unique_fd.h>

#include "test/blob_utils.h"

struct BlobFile {
  BlobFile(std::unique_ptr<blobfs::BlobInfo> i, size_t writes_remaining)
      : info(std::move(i)), writes_remaining(writes_remaining) {
    bytes_remaining = info->size_data;
  }
  BlobFile() {}

  std::unique_ptr<blobfs::BlobInfo> info;
  fbl::unique_fd fd;
  size_t writes_remaining;
  size_t bytes_remaining;
};

// Keeps track of a collection of blobfs files, doing pseudo-random operations
// with them, in a thread-safe way.
//
// The basic mode of operation simply generates traffic for the filesystem.
class BlobList {
 public:
  explicit BlobList(const char* mount_path) : mount_path_(mount_path) {}

  // Cycles through |num_opertaions| filesystem operations. |rand_state| should
  // be initialized to the desired seed for the random operations (and data).
  void GenerateLoad(uint32_t num_operations, unsigned int* rand_state);

  // Verifies the contents of all fully-written blobs in the list.
  void VerifyFiles();

  // Closes the file descriptors for all the blobs in the blob list.
  //
  // This function is not thread-safe in the sense that if other threads are
  // doing operations, when this function returns there may be open files.
  void CloseFiles();

 private:
  enum class QueueId : uint32_t;

  BlobFile GetFileFrom(QueueId queue);
  void PushFileInto(QueueId queue, BlobFile file);
  QueueId GetRandomQueue(unsigned int* rand_state) const;
  void CloseFilesFromQueue(QueueId queue);

  // Adds a new blob entry to the list. The blob's data will be filled in
  // |num_writes| operations.
  void CreateBlob(unsigned int* rand_state, size_t num_writes = 1);

  // Truncates the blob on disk to the size of the randomly generated data.
  void TruncateBlob();

  // Writes random data to the blob.
  void WriteData();

  // Reads and verifies the file contents.
  void ReadData();

  // Removes the blob from the list and underlying filesystem.
  void UnlinkBlob(unsigned int* rand_state);

  // Closes the file and re-opens it.
  void ReopenBlob();

  const char* mount_path_;
  fbl::Mutex list_lock_;
  uint32_t blob_count_ TA_GUARDED(list_lock_) = 0;
  std::array<std::list<BlobFile>, 3> lists_ TA_GUARDED(list_lock_);  // One per QueueId.
};

#endif  // SRC_STORAGE_BLOBFS_TEST_INTEGRATION_LOAD_GENERATOR_H_
