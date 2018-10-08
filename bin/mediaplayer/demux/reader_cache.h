// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_DEMUX_READER_CACHE_H_
#define GARNET_BIN_MEDIAPLAYER_DEMUX_READER_CACHE_H_

#include <memory>

#include "garnet/bin/mediaplayer/demux/reader.h"
#include "garnet/bin/mediaplayer/demux/sparse_byte_buffer.h"
#include "garnet/bin/mediaplayer/util/incident.h"
#include "lib/async/dispatcher.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace media_player {

// ReaderCache implements Reader against a dynamic in-memory cache of an
// upstream Reader's asset.
//
// ReaderCache is backed by a SparseByteBuffer which tracks holes (spans of the
// asset that haven't been read) and regions (spans of the asset that have been
// read). See SparseByteBuffer for details.
//
// ReaderCache will serve ReadAt requests from its in-memory cache, and maintain
// its cache asynchronously using the upstream reader on a schedule determined
// by the cache options (see SetCacheOptions).
class ReaderCache : public Reader,
                    public std::enable_shared_from_this<ReaderCache> {
 public:
  static std::shared_ptr<ReaderCache> Create(
      std::shared_ptr<Reader> upstream_reader);

  ReaderCache(std::shared_ptr<Reader> upstream_reader);

  ~ReaderCache() override;

  // Reader implementation.
  void Describe(DescribeCallback callback) override;

  void ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
              ReadAtCallback callback) override;

  // Configures the ReaderCache to respect the given memory budget.
  //   |capacity| is the amount of memory ReaderCache is allowed to use for
  //              caching the upstream Reader's content.
  //   |max_backtrack| is the amount of memory (< capacity) that ReaderCache
  //                   will maintain _behind_ the ReadAt point (for skipping
  //                   back).
  //   |chunk_size| is the size of read chunks ReaderCache will use when reading
  //                from upstream.
  void SetCacheOptions(size_t capacity, size_t max_backtrack,
                       size_t chunk_size);

 private:
  // Loads if
  //   1. No load is in progress already.
  //   2. There are holes in the desired cache range for this position which
  //      require filling.
  // Starts a load from the upstream Reader into our buffer over the given
  // range.
  //   1. If requested, clean up memory outside the desired range to pay for the
  //      new allocations.
  //   2. Makes async calls for the upstream Reader to fill all the holes in the
  //      desired cache range.
  //   3. Running any ReadAt call queued on this reload.
  void MaybeStartLoadForPosition(size_t position);

  // Makes async calls to the upstream Reader to fill the given holes in our
  // underlying buffer. Calls callback on completion.
  void FillHoles(std::vector<SparseByteBuffer::Hole> holes,
                 fit::closure callback);

  // Calculates the desired cache range according to our cache options around
  // the requested read position.
  std::pair<size_t, size_t> CalculateCacheRange(size_t position);

  // |buffer_| is the underlying storage for the cache.
  SparseByteBuffer buffer_;
  Result last_result_;

  Incident describe_is_complete_;
  Incident load_is_complete_;

  // These values are stable after |describe_is_complete_|.
  std::shared_ptr<Reader> upstream_reader_;
  size_t upstream_size_;
  // TODO(turnage): Respect can_seek_ == false in upstream reader.
  bool upstream_can_seek_;

  size_t capacity_ = 16 * 1024 * 1024;
  size_t max_backtrack_ = 0;
  size_t chunk_size_ = 512 * 1024;

  async_dispatcher_t* dispatcher_;

  bool load_in_progress_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_DEMUX_READER_CACHE_H_
