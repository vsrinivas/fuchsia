// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_CACHE_POLICY_H_
#define SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_CACHE_POLICY_H_

namespace blobfs {

// CachePolicy describes the techniques used to cache blobs in memory, avoiding re-reading and
// re-verifying them from disk.
enum class CachePolicy {
  // When all strong references to a node are closed, |ActivateLowMemory()| is invoked.
  //
  // This option avoids using memory for any longer than it needs to, but may result in higher
  // performance penalties for blobs that are frequently opened and closed.
  EvictImmediately,

  // The node is never evicted from memory, unless it has been fully deleted and there are no
  // additional references.
  //
  // This option costs a significant amount of memory, but it results in high performance.
  NeverEvict,
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_CACHE_POLICY_H_
