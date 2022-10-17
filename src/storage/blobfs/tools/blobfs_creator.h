// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TOOLS_BLOBFS_CREATOR_H_
#define SRC_STORAGE_BLOBFS_TOOLS_BLOBFS_CREATOR_H_

#include <lib/fit/defer.h>

#include <map>
#include <optional>
#include <vector>

#include <fbl/array.h>
#include <fbl/vector.h>
#include <fs-host/common.h>

#include "src/lib/chunked-compression/multithreaded-chunked-compressor.h"
#include "src/lib/digest/digest.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/host.h"

class BlobfsCreator : public FsCreator {
 public:
  BlobfsCreator() : FsCreator(blobfs::kMinimumDataBlocks) {}

 private:
  // Parent overrides:
  zx_status_t Usage() override;
  const char* GetToolName() override { return "blobfs"; }
  bool IsCommandValid(Command command) override;
  bool IsOptionValid(Option option) override;
  bool IsArgumentValid(Argument argument) override;

  // Identify blobs to be operated on, populating the internal
  // |blob_list_|.
  zx_status_t ProcessManifestLine(FILE* manifest, const char* dir_path) override;
  zx_status_t ProcessCustom(int argc, char** argv, uint8_t* processed) override;

  // Generate BlobInfo for a given blob path.
  zx::result<blobfs::BlobInfo> ProcessBlobToBlobInfo(
      const std::filesystem::path& path,
      std::optional<chunked_compression::MultithreadedChunkedCompressor>& compressor);

  // Calculates merkle trees for the processed blobs, and determines
  // the total size of the underlying storage necessary to contain them.
  zx_status_t CalculateRequiredSize(off_t* out) override;

  // TODO(planders): Add ls support for blobfs.
  zx_status_t Mkfs() override;
  zx_status_t Fsck() override;
  zx_status_t UsedDataSize() override;
  zx_status_t UsedInodes() override;
  zx_status_t UsedSize() override;
  zx_status_t Add() override;

  // List of all blobs to be copied into blobfs.
  std::vector<std::filesystem::path> blob_list_;

  // Guard for synchronizing the multithreaded file and compression operations.
  std::mutex blob_info_lock_;

  // A list of Blob Information for blobs in |blob_list_|.
  std::map<digest::Digest, blobfs::BlobInfo> blob_info_list_ __TA_GUARDED(blob_info_lock_);

  // The format blobfs should use to store blobs.
  blobfs::BlobLayoutFormat blob_layout_format_ = blobfs::BlobLayoutFormat::kCompactMerkleTreeAtEnd;

  // When adding blobs, will generate a compressed version of the blob in the internal format at the
  // specified prefix.
  std::string compressed_copy_prefix_;

  // The number of inodes required in the resultant blobfs image.
  uint64_t required_inodes_ = 0;
};

#endif  // SRC_STORAGE_BLOBFS_TOOLS_BLOBFS_CREATOR_H_
