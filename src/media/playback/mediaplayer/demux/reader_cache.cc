// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/demux/reader_cache.h"

#include <lib/syslog/cpp/macros.h>

#include "lib/async/cpp/task.h"
#include "lib/async/default.h"

namespace media_player {
namespace {

static constexpr size_t kDefaultChunkSize = 256 * 1024;

// When calculating how much to read from the upstream reader before the demuxer
// will miss the cache, we multiply by this factor to be conservative.
static constexpr float kConservativeFactor = 0.8;

static constexpr size_t kByteRateMaxSamples = 8;

}  // namespace

// static
std::shared_ptr<ReaderCache> ReaderCache::Create(std::shared_ptr<Reader> upstream_reader) {
  return std::make_shared<ReaderCache>(upstream_reader);
}

ReaderCache::ReaderCache(std::shared_ptr<Reader> upstream_reader)
    : upstream_reader_(upstream_reader),
      dispatcher_(async_get_default_dispatcher()),
      demux_byte_rate_(kByteRateMaxSamples),
      upstream_reader_byte_rate_(kByteRateMaxSamples) {
  upstream_reader_->Describe(
      [this, upstream_reader](zx_status_t status, size_t size, bool can_seek) {
        upstream_size_ = size;
        upstream_can_seek_ = can_seek;
        last_status_ = status;
        describe_is_complete_.Occur();
      });
}

ReaderCache::~ReaderCache() {}

void ReaderCache::Describe(DescribeCallback callback) {
  describe_is_complete_.When([this, callback = std::move(callback)]() mutable {
    callback(last_status_, upstream_size_, upstream_can_seek_);
  });
}

void ReaderCache::ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
                         ReadAtCallback callback) {
  FX_DCHECK(buffer);
  FX_DCHECK(bytes_to_read > 0);

  describe_is_complete_.When(
      [this, position, buffer, bytes_to_read, callback = std::move(callback)]() mutable {
        if (demux_sampler_) {
          demux_byte_rate_.AddSample(
              ByteRateEstimator::ByteRateSampler::FinishSample(std::move(*demux_sampler_)));
        }

        if (!buffer_) {
          buffer_ = SlidingBuffer(capacity_);
        }

        ServeReadAtRequest({
            .callback = std::move(callback),
            .original_position = position,
            .total_bytes = bytes_to_read,
            .position = position,
            .buffer = buffer,
            .bytes_to_read = bytes_to_read,
        });
      });
}

void ReaderCache::SetCacheOptions(size_t capacity, size_t max_backtrack) {
  FX_DCHECK(!load_in_progress_) << "SetCacheOptions cannot be called while a load is"
                                   " in progress.";

  buffer_ = SlidingBuffer(capacity);
  capacity_ = capacity;
  max_backtrack_ = max_backtrack;
}

void ReaderCache::ServeReadAtRequest(ReaderCache::ReadAtRequest request) {
  FX_DCHECK(buffer_);
  FX_DCHECK(request.buffer);
  FX_DCHECK(request.callback);
  FX_DCHECK(request.position < upstream_size_);

  size_t bytes_read = buffer_->Read(request.position, request.buffer, request.bytes_to_read);

  size_t remaining_bytes = upstream_size_ - request.position;
  if ((bytes_read == request.bytes_to_read) || (bytes_read == remaining_bytes)) {
    demux_sampler_ = ByteRateEstimator::ByteRateSampler::StartSample(bytes_read);
    const size_t bytes_we_will_not_read = request.bytes_to_read - bytes_read;
    request.callback(ZX_OK, request.total_bytes - bytes_we_will_not_read);
    return;
  }

  StartLoadForPosition(
      request.position + bytes_read,
      [this, bytes_read, request = std::move(request)](zx_status_t status) mutable {
        if (status != ZX_OK) {
          request.callback(status, request.position + bytes_read - request.original_position);
          return;
        }

        ServeReadAtRequest({
            .callback = std::move(request.callback),
            .original_position = request.original_position,
            .total_bytes = request.total_bytes,
            .position = request.position + bytes_read,
            .buffer = request.buffer + bytes_read,
            .bytes_to_read = request.bytes_to_read - bytes_read,
        });
      });
}

void ReaderCache::StartLoadForPosition(size_t position,
                                       fit::function<void(zx_status_t)> load_callback) {
  FX_DCHECK(buffer_);
  FX_DCHECK(!load_in_progress_);
  load_in_progress_ = true;

  auto load_range = CalculateLoadRange(position);
  FX_DCHECK(load_range) << "The media is fully cached for the read, but a load was requested.";

  auto [load_start, load_size] = *load_range;
  auto holes = buffer_->Slide(load_start, std::min({load_size, upstream_size_ - load_start,
                                                    buffer_->capacity() - max_backtrack_}));

  FillHoles(holes, [this, load_callback = std::move(load_callback)](zx_status_t status) mutable {
    load_in_progress_ = false;
    if (load_callback) {
      load_callback(status);
    }
  });
}

std::optional<std::pair<size_t, size_t>> ReaderCache::CalculateLoadRange(size_t position) {
  FX_DCHECK(buffer_);

  auto next_missing_byte = buffer_->NextMissingByte(position);
  if (next_missing_byte == upstream_size_) {
    // The media is buffered until the end.
    return std::nullopt;
  }
  size_t bytes_until_demux_misses = next_missing_byte - position;

  const std::pair<size_t, size_t> defaultRange = {position, kDefaultChunkSize};

  std::optional<float> demux_byte_rate_estimate = demux_byte_rate_.Estimate();
  std::optional<float> upstream_reader_byte_rate_estimate = upstream_reader_byte_rate_.Estimate();
  if (!demux_byte_rate_estimate || !upstream_reader_byte_rate_estimate) {
    // We don't have enough information to make an informed estimation so we
    // defer to our configuration.
    return defaultRange;
  }

  float time_until_demux_misses = float(bytes_until_demux_misses) / (*demux_byte_rate_estimate);
  float bytes_we_can_read_before_demux_misses =
      time_until_demux_misses * (*upstream_reader_byte_rate_estimate) * kConservativeFactor;

  if (bytes_we_can_read_before_demux_misses < 1) {
    // Cache misses are inevitable. We fall back to our configuration in this
    // case to avoid many small waits.
    return defaultRange;
  }

  return std::make_pair(position, size_t(bytes_we_can_read_before_demux_misses));
}

void ReaderCache::FillHoles(std::vector<SlidingBuffer::Block> holes,
                            fit::function<void(zx_status_t)> callback) {
  upstream_reader_sampler_ = ByteRateEstimator::ByteRateSampler::StartSample(holes.back().size);
  upstream_reader_->ReadAt(
      holes.back().start, holes.back().buffer, holes.back().size,
      [this, holes, callback = std::move(callback)](zx_status_t status, size_t bytes_read) mutable {
        last_status_ = status;
        if (status == ZX_OK && upstream_reader_sampler_) {
          upstream_reader_byte_rate_.AddSample(ByteRateEstimator::ByteRateSampler::FinishSample(
              std::move(*upstream_reader_sampler_)));
        }
        upstream_reader_sampler_ = std::nullopt;

        holes.pop_back();
        if (holes.empty()) {
          callback(status);
          return;
        }

        FillHoles(holes, std::move(callback));
      });
}

}  // namespace media_player
