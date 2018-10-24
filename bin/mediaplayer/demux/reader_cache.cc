// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/demux/reader_cache.h"

#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace {

// TODO(turnage): Intelligently choose this by observing read times from the
// upstream reader and whether our reads block the demuxer.
static constexpr size_t kChunkSize = 256 * 1024;

}  // namespace

// static
std::shared_ptr<ReaderCache> ReaderCache::Create(
    std::shared_ptr<Reader> upstream_reader) {
  return std::make_shared<ReaderCache>(upstream_reader);
}

ReaderCache::ReaderCache(std::shared_ptr<Reader> upstream_reader)
    : upstream_reader_(upstream_reader),
      dispatcher_(async_get_default_dispatcher()) {
  upstream_reader_->Describe([this, upstream_reader](Result result, size_t size,
                                                     bool can_seek) {
    upstream_size_ = size;
    upstream_can_seek_ = can_seek;
    last_result_ = result;
    buffer_.Initialize(size);

    async::PostTask(dispatcher_, [this]() { MaybeStartLoadForPosition(0); });

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

    size_t bytes_read = buffer_.ReadRange(position, bytes_to_read, buffer);

    MaybeStartLoadForPosition(position);

    size_t remaining_bytes = upstream_size_ - position;
    if ((bytes_read == bytes_to_read) || (bytes_read == remaining_bytes)) {
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
  std::pair<size_t, size_t> cache_range = CalculateCacheRange(position);
  load_position_ = cache_range.first;

  if (load_in_progress_) {
    return;
  }

  std::vector<SparseByteBuffer::Hole> holes_in_cache =
      buffer_.FindOrCreateHolesInRange(cache_range.first, cache_range.second);

  if (holes_in_cache.empty()) {
    return;
  }

  load_in_progress_ = true;
  size_t bytes_needed = 0;
  for (auto& hole : holes_in_cache) {
    bytes_needed += hole.size();
  }

  buffer_.CleanUpExcept(bytes_needed, cache_range.first, cache_range.second);
  holes_in_cache =
      buffer_.FindOrCreateHolesInRange(cache_range.first, cache_range.second);

  FillHoles(holes_in_cache, cache_range.first, [this, cache_range]() {
    load_in_progress_ = false;
    load_is_complete_.Occur();
    load_is_complete_.Reset();
  });
}

void ReaderCache::FillHoles(std::vector<SparseByteBuffer::Hole> holes,
                            size_t load_position, fit::closure callback) {
  std::vector<uint8_t> buffer(holes.back().size());
  upstream_reader_->ReadAt(
      holes.back().position(), buffer.data(), holes.back().size(),
      [this, holes, load_position, buffer = std::move(buffer),
       callback = std::move(callback)](Result result,
                                       size_t bytes_read) mutable {
        last_result_ = result;
        if (result != Result::kOk) {
          FXL_LOG(ERROR) << "ReadAt failed!";
        }
        buffer_.Fill(holes.back(), std::move(buffer));

        holes.pop_back();
        if (holes.empty()) {
          callback();
          return;
        }

        if (load_position != load_position_) {
          callback();
          return;
        }

        FillHoles(holes, load_position, std::move(callback));
      });
}

std::pair<size_t, size_t> ReaderCache::CalculateCacheRange(size_t position) {
  if (upstream_size_ <= capacity_) {
    return {0, upstream_size_};
  }

  size_t chunk_position = position - (position % kChunkSize);
  size_t cache_start =
      chunk_position > max_backtrack_ ? chunk_position - max_backtrack_ : 0;

  return {cache_start,
          std::min(capacity_ - max_backtrack_, upstream_size_ - cache_start)};
}

}  // namespace media_player
