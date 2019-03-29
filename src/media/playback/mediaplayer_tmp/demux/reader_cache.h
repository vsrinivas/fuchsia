// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_DEMUX_READER_CACHE_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_DEMUX_READER_CACHE_H_

#include <memory>
#include <optional>

#include "lib/async/dispatcher.h"
#include "src/lib/fxl/synchronization/thread_checker.h"
#include "src/media/playback/mediaplayer_tmp/demux/byte_rate_estimator.h"
#include "src/media/playback/mediaplayer_tmp/demux/reader.h"
#include "src/media/playback/mediaplayer_tmp/demux/sliding_buffer.h"
#include "src/media/playback/mediaplayer_tmp/util/incident.h"

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

  // Configures the |ReaderCache| to respect the given memory budget. |capacity|
  // is the amount of memory |ReaderCache| is allowed to spend caching the
  // upstream |Reader|'s content. |max_backtrack| is the amount of memory that
  // |ReaderCache| will maintain behind the |ReadAt| point (for skipping back).
  // |max_backtrack| must be less than |capacity|.
  void SetCacheOptions(size_t capacity, size_t max_backtrack);

 private:
  struct ReadAtRequest {
    ReadAtCallback callback;
    size_t original_position;
    size_t total_bytes;
    size_t position;
    uint8_t* buffer;
    size_t bytes_to_read;
  };

  void ServeReadAtRequest(ReadAtRequest request);

  // Starts a load from the upstream |Reader| into our buffer over the given
  // range. 1) Cleans up memory outside the desired range to pay for the new
  // allocations. 2) Makes async calls for the upstream |Reader| to fill all the
  // holes in the desired cache range. 3) Invokes |load_callback| on completion
  // of the load.
  void StartLoadForPosition(size_t position,
                            fit::function<void(Result)> load_callback);

  // Estimates load range based on observations of the input (upstream source)
  // and output (demux requests) byte rates. Returns std::nullopt if there is
  // no need to load for the given position.
  std::optional<std::pair<size_t, size_t>> CalculateLoadRange(size_t position);

  // Makes async calls to the upstream Reader to fill the given holes in our
  // underlying buffer. Calls callback on completion.
  void FillHoles(std::vector<SlidingBuffer::Block> holes,
                 fit::function<void(Result)> callback);

  // Calculates the desired cache range according to our cache options around
  // the requested read position.
  std::pair<size_t, size_t> CalculateCacheRange(size_t position);

  // |buffer_| is the underlying storage for the cache.
  std::optional<SlidingBuffer> buffer_;
  Result last_result_;

  Incident describe_is_complete_;

  // These values are stable after |describe_is_complete_|.
  std::shared_ptr<Reader> upstream_reader_;
  size_t upstream_size_;
  // TODO(turnage): Respect can_seek_ == false in upstream reader.
  bool upstream_can_seek_;

  async_dispatcher_t* dispatcher_;
  bool load_in_progress_ = false;

  size_t capacity_ = 1 * 1024 * 1024;
  size_t max_backtrack_ = 0;

  ByteRateEstimator demux_byte_rate_;
  std::optional<ByteRateEstimator::ByteRateSampler> demux_sampler_;
  ByteRateEstimator upstream_reader_byte_rate_;
  std::optional<ByteRateEstimator::ByteRateSampler> upstream_reader_sampler_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_DEMUX_READER_CACHE_H_
