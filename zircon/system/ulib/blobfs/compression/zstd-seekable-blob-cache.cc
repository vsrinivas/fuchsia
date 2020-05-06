// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-seekable-blob-cache.h"

#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <list>
#include <memory>

#include "zstd-seekable-blob.h"

// TODO: This is just for an experiment.
#include <fs/trace.h>

namespace blobfs {

namespace {

const std::unique_ptr<ZSTDSeekableBlob> kNullBlobPtr = nullptr;

// TODO: This is just for an experiment.
uint64_t gNumBlobCacheWrites = 0;
uint64_t gNumBlobCacheReads = 0;
uint64_t gNumBlobCacheHits = 0;

}  // namespace

ZSTDSeekableLRUBlobCache::ZSTDSeekableLRUBlobCache(size_t max_size) : max_size_(max_size) {}

const std::unique_ptr<ZSTDSeekableBlob>& ZSTDSeekableLRUBlobCache::WriteBlob(
    std::unique_ptr<ZSTDSeekableBlob> blob) {
  if (blobs_.size() == max_size_) {
    blobs_.pop_front();
  }
  blobs_.push_back(std::move(blob));
  gNumBlobCacheWrites++;
  return blobs_.back();
}

const std::unique_ptr<ZSTDSeekableBlob>& ZSTDSeekableLRUBlobCache::ReadBlob(uint32_t node_index) {
  // TODO: This is just for an experiment.
  // if (gNumBlobCacheReads % 1000 == 0) {
  //   FS_TRACE_ERROR(
  //     "\n\n{\"blob_cache_reads\": %lu, \"blob_cache_writes\": %lu, \"blob_cache_hits\":
  //     %lu}\n\n", gNumBlobCacheReads, gNumBlobCacheWrites, gNumBlobCacheHits);
  // }

  gNumBlobCacheReads++;
  for (const auto& blob : blobs_) {
    if (blob->node_index() == node_index) {
      gNumBlobCacheHits++;
      return blob;
    }
  }
  return kNullBlobPtr;
}

}  // namespace blobfs
