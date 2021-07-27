// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_BLOBFS_BLOBFS_H_
#define ZIRCON_TOOLS_BLOBFS_BLOBFS_H_

#include <lib/fit/defer.h>

#include <vector>

#include <fbl/array.h>
#include <fbl/vector.h>
#include <fs-host/common.h>

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

  // A comparison function used to quickly compare BlobInfo.
  struct DigestCompare {
    inline bool operator()(const blobfs::BlobInfo& lhs, const blobfs::BlobInfo& rhs) const {
      return lhs.GetDigest() < rhs.GetDigest();
    }
  };

  // List of all blobs to be copied into blobfs.
  std::vector<std::filesystem::path> blob_list_;

  // A list of Blob Information for blobs in |blob_list_|.
  std::vector<blobfs::BlobInfo> blob_info_list_;

  // The format blobfs should use to store blobs.
  blobfs::BlobLayoutFormat blob_layout_format_ = blobfs::BlobLayoutFormat::kCompactMerkleTreeAtEnd;

  // The number of inodes required in the resultant blobfs image.
  uint64_t required_inodes_ = 0;
};

#endif  // ZIRCON_TOOLS_BLOBFS_BLOBFS_H_
