// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/demux/reader_cache.h"

#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace {

static constexpr size_t kDefaultChunkSize = 256 * 1024;

// When calculating how much to read from the upstream reader before the demuxer
// will miss the cache, we multiply by this factor to be conservative.
static constexpr float kConservativeFactor = 0.8;

static constexpr size_t kByteRateMaxSamples = 8;

}  // namespace

// static
std::shared_ptr<ReaderCache> ReaderCache::Create(
    std::shared_ptr<Reader> upstream_reader) {
  return std::make_shared<ReaderCache>(upstream_reader);
}

ReaderCache::ReaderCache(std::shared_ptr<Reader> upstream_reader)
    : upstream_reader_(upstream_reader),
      dispatcher_(async_get_default_dispatcher()),
      demux_byte_rate_(kByteRateMaxSamples),
      upstream_reader_byte_rate_(kByteRateMaxSamples) {
  upstream_reader_->Describe(
      [this, upstream_reader](Result result, size_t size, bool can_seek) {
        upstream_size_ = size;
        upstream_can_seek_ = can_seek;
        last_result_ = result;
        buffer_.Initialize(upstream_size_);

        MaybeStartLoadForPosition(0);
        load_is_complete_.When([this] {
          MaybeStartLoadForPosition(upstream_size_ - kDefaultChunkSize);
        });

        describe_is_complete_.Occur();
      });
}

ReaderCache::~ReaderCache() {}

void ReaderCache::Describe(DescribeCallback callback) {
  describe_is_complete_.When([this, callback = std::move(callback)]() mutable {
    callback(last_result_, upstream_size_, upstream_can_seek_);
  });
}

void ReaderCache::ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
                         ReadAtCallback callback) {
  FXL_DCHECK(buffer);
  FXL_DCHECK(bytes_to_read > 0);

  describe_is_complete_.When([this, position, buffer, bytes_to_read,
                              callback = std::move(callback)]() mutable {
    FXL_DCHECK(position < upstream_size_);

    if (demux_sampler_) {
      demux_byte_rate_.AddSample(
          ByteRateEstimator::ByteRateSampler::FinishSample(
              std::move(*demux_sampler_)));
    }
    size_t bytes_read = buffer_.ReadRange(position, bytes_to_read, buffer);

    MaybeStartLoadForPosition(position);

    size_t remaining_bytes = upstream_size_ - position;
    if ((bytes_read == bytes_to_read) || (bytes_read == remaining_bytes)) {
      demux_sampler_ =
          ByteRateEstimator::ByteRateSampler::StartSample(bytes_read);
      callback(Result::kOk, bytes_read);
      return;
    }

    load_is_complete_.When([this, position, buffer, bytes_to_read,
                            callback = std::move(callback)]() mutable {
      ReadAt(position, buffer, bytes_to_read, std::move(callback));
    });
  });
}

void ReaderCache::SetCacheOptions(size_t capacity, size_t max_backtrack) {
  capacity_ = capacity;
  max_backtrack_ = max_backtrack;
}

void ReaderCache::MaybeStartLoadForPosition(size_t position) {
  if (load_in_progress_) {
    return;
  }

  auto load_range = CalculateLoadRange(position);
  if (!load_range) {
    return;
  }

  auto [load_start, load_end] = *load_range;
  size_t bytes_needed = buffer_.BytesMissingInRange(load_start, load_end);
  if (bytes_needed == 0) {
    return;
  }
  load_in_progress_ = true;

  size_t bytes_available = capacity_ - buffer_.BytesStored();
  if (bytes_needed > bytes_available) {
    auto [cache_start, cache_end] = CalculateCacheRange(position);
    buffer_.CleanUpExcept(bytes_needed - bytes_available, cache_start,
                          cache_end);
  }
  auto holes_in_cache = buffer_.FindOrCreateHolesInRange(load_start, load_end);

  FillHoles(holes_in_cache, [this]() {
    load_in_progress_ = false;
    load_is_complete_.Occur();
    load_is_complete_.Reset();
  });
}

std::optional<std::pair<size_t, size_t>> ReaderCache::CalculateLoadRange(
    size_t position) {
  auto next_missing_byte = buffer_.NextMissingByte(position);
  if (!next_missing_byte) {
    // The media is buffered until the end.
    return std::nullopt;
  }
  FXL_DCHECK(*next_missing_byte >= position);
  size_t bytes_until_demux_misses = (*next_missing_byte) - position;

  std::optional<float> demux_byte_rate_estimate = demux_byte_rate_.Estimate();
  std::optional<float> upstream_reader_byte_rate_estimate =
      upstream_reader_byte_rate_.Estimate();
  if (!demux_byte_rate_estimate || !upstream_reader_byte_rate_estimate) {
    return std::make_pair(position, position + kDefaultChunkSize);
  }

  float time_until_demux_misses =
      float(bytes_until_demux_misses) / (*demux_byte_rate_estimate);
  float bytes_we_can_read_before_demux_misses =
      time_until_demux_misses * (*upstream_reader_byte_rate_estimate) *
      kConservativeFactor;

  if (bytes_we_can_read_before_demux_misses < 1) {
    // Cache misses are inevitable. We will defer to our given configuration
    // here to prevent adding in many short buffering periods.
    return CalculateCacheRange(position);
  }

  return std::make_pair(
      position, position + size_t(bytes_we_can_read_before_demux_misses));
}

void ReaderCache::FillHoles(std::vector<SparseByteBuffer::Hole> holes,
                            fit::closure callback) {
  std::vector<uint8_t> buffer(holes.back().size());
  upstream_reader_sampler_ =
      ByteRateEstimator::ByteRateSampler::StartSample(holes.back().size());
  upstream_reader_->ReadAt(
      holes.back().position(), buffer.data(), holes.back().size(),
      [this, holes, buffer = std::move(buffer), callback = std::move(callback)](
          Result result, size_t bytes_read) mutable {
        last_result_ = result;
        if (result != Result::kOk) {
          FXL_LOG(ERROR) << "ReadAt failed!";
        }
        buffer_.Fill(holes.back(), std::move(buffer));
        if (upstream_reader_sampler_) {
          upstream_reader_byte_rate_.AddSample(
              ByteRateEstimator::ByteRateSampler::FinishSample(
                  std::move(*upstream_reader_sampler_)));
        }

        holes.pop_back();
        if (holes.empty()) {
          callback();
          return;
        }

        FillHoles(holes, std::move(callback));
      });
}

std::pair<size_t, size_t> ReaderCache::CalculateCacheRange(size_t position) {
  if (upstream_size_ <= capacity_) {
    return {0, upstream_size_};
  }

  size_t chunk_position = position - (position % kDefaultChunkSize);
  size_t cache_start =
      chunk_position > max_backtrack_ ? chunk_position - max_backtrack_ : 0;

  return {cache_start,
          std::min(capacity_ - max_backtrack_, upstream_size_ - cache_start)};
}

}  // namespace media_player
