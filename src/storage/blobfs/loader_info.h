// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_LOADER_INFO_H_
#define SRC_STORAGE_BLOBFS_LOADER_INFO_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <memory>

#include "src/storage/blobfs/blob_verifier.h"
#include "src/storage/blobfs/compression/seekable_decompressor.h"

namespace blobfs {

// Info required by to read in and verify pages.
struct LoaderInfo {
  // Inode index for the blob.
  uint32_t node_index = 0;

  // Block offset (in bytes) the data starts at.
  uint64_t data_start_bytes = 0;

  // Total length of the data. The |verifier| must be set up to verify this length.
  uint64_t data_length_bytes = 0;

  // Used to verify the pages as they are read in.
  // TODO(fxbug.dev/44742): Make BlobVerifier movable, unwrap from unique_ptr.
  std::unique_ptr<BlobVerifier> verifier;

  // An optional decompressor used by the chunked compression strategy. The decompressor is invoked
  // on the raw bytes received from the disk. If unset, blob data is assumed to be uncompressed.
  std::unique_ptr<SeekableDecompressor> decompressor;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_LOADER_INFO_H_
