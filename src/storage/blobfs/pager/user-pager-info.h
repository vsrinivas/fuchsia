// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_PAGER_USER_PAGER_INFO_H_
#define SRC_STORAGE_BLOBFS_PAGER_USER_PAGER_INFO_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <memory>

#include "../blob-verifier.h"
#include "../compression/seekable-decompressor.h"

namespace blobfs {
namespace pager {

// Info required by the user pager to read in and verify pages.
// Initialized by the PageWatcher and passed on to the UserPager.
struct UserPagerInfo {
  // Unique identifier used by UserPager to identify the data source on the underlying block
  // device.
  uint32_t identifier = 0;
  // Block offset (in bytes) the data starts at. Used to inform the UserPager of the offset it
  // should start issuing reads from.
  uint64_t data_start_bytes = 0;
  // Total length of the data. The |verifier| must be set up to verify this length.
  uint64_t data_length_bytes = 0;
  // Used to verify the pages as they are read in.
  // TODO(fxbug.dev/44742): Make BlobVerifier movable, unwrap from unique_ptr.
  std::unique_ptr<BlobVerifier> verifier;
  // An optional decompressor used by the chunked compression strategy. The decompressor is invoked
  // on the raw bytes received from the disk. If unset, blob data is assumed to be managed via some
  // other compression strategy (including the "uncompressed" strategy).
  std::unique_ptr<SeekableDecompressor> decompressor;
};

}  // namespace pager
}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_PAGER_USER_PAGER_INFO_H_
