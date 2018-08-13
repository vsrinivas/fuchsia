// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/demux/reader_cache.h"

#include "lib/fxl/logging.h"

namespace media_player {

// static
std::shared_ptr<ReaderCache> ReaderCache::Create(
    std::shared_ptr<Reader> upstream_reader) {
  return std::make_shared<ReaderCache>(upstream_reader);
}

ReaderCache::ReaderCache(std::shared_ptr<Reader> upstream_reader) {
  upstream_reader->Describe(
      [this, upstream_reader](Result result, size_t size, bool can_seek) {
        store_.Initialize(result, size, can_seek);

        describe_is_complete_.Occur();

        if (result == Result::kOk) {
          intake_.Start(std::weak_ptr<ReaderCache>(shared_from_this()),
                        upstream_reader);
        }
      });
}

ReaderCache::~ReaderCache() {}

void ReaderCache::Describe(DescribeCallback callback) {
  describe_is_complete_.When([this, callback = std::move(callback)]() mutable {
    store_.Describe(std::move(callback));
  });
}

void ReaderCache::ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
                         ReadAtCallback callback) {
  FXL_DCHECK(buffer);
  FXL_DCHECK(bytes_to_read > 0);

  read_at_request_.Start(position, buffer, bytes_to_read, std::move(callback));

  describe_is_complete_.When(
      [this]() { store_.SetReadAtRequest(&read_at_request_); });
}

ReaderCache::ReadAtRequest::ReadAtRequest() { in_progress_ = false; }

ReaderCache::ReadAtRequest::~ReadAtRequest() {}

void ReaderCache::ReadAtRequest::Start(size_t position, uint8_t* buffer,
                                       size_t bytes_to_read,
                                       ReadAtCallback callback) {
  FXL_DCHECK(!in_progress_) << "concurrent calls to ReadAt are not allowed";
  in_progress_ = true;
  position_ = position;
  buffer_ = buffer;
  original_bytes_to_read_ = bytes_to_read;
  remaining_bytes_to_read_ = bytes_to_read;
  callback_ = std::move(callback);
}

void ReaderCache::ReadAtRequest::CopyFrom(uint8_t* source, size_t byte_count) {
  FXL_DCHECK(source);
  FXL_DCHECK(byte_count <= remaining_bytes_to_read_);

  std::memcpy(buffer_, source, byte_count);

  position_ += byte_count;
  buffer_ += byte_count;
  remaining_bytes_to_read_ -= byte_count;
}

void ReaderCache::ReadAtRequest::Complete(Result result) {
  FXL_DCHECK(original_bytes_to_read_ >= remaining_bytes_to_read_);
  size_t bytes_read = original_bytes_to_read_ - remaining_bytes_to_read_;

  // If we've read 0 bytes, something must be wrong.
  FXL_DCHECK((bytes_read == 0) == (result != Result::kOk));

  ReadAtCallback callback;
  callback_.swap(callback);
  in_progress_ = false;
  callback(result, bytes_read);
}

ReaderCache::Store::Store() {}

ReaderCache::Store::~Store() {}

void ReaderCache::Store::Initialize(Result result, size_t size, bool can_seek) {
  std::lock_guard<std::mutex> locker(mutex_);

  result_ = result;
  size_ = size;
  can_seek_ = can_seek;

  // Create one hole spanning the entire asset.
  sparse_byte_buffer_.Initialize(size_);
  intake_hole_ = sparse_byte_buffer_.FindHoleContaining(0);
  read_hole_ = sparse_byte_buffer_.null_hole();
  read_region_ = sparse_byte_buffer_.null_region();
}

void ReaderCache::Store::Describe(DescribeCallback callback) {
  Result result;

  {
    std::lock_guard<std::mutex> locker(mutex_);
    result = result_;
  }

  callback(result, size_, can_seek_);
}

void ReaderCache::Store::SetReadAtRequest(ReadAtRequest* request) {
  mutex_.lock();

  FXL_DCHECK(read_request_ == nullptr);
  FXL_DCHECK(request->position() < size_);
  FXL_DCHECK(request->remaining_bytes_to_read() > 0);

  read_request_ = request;
  read_request_position_ = request->position();
  read_request_remaining_bytes_ = request->remaining_bytes_to_read();

  if (read_request_position_ + read_request_remaining_bytes_ > size_) {
    read_request_remaining_bytes_ = size_ - read_request_position_;
  }

  ServeRequest();  // unlocks mutex_
}

size_t ReaderCache::Store::GetIntakePositionAndSize(size_t* size_out) {
  FXL_DCHECK(size_out);

  std::lock_guard<std::mutex> locker(mutex_);

  size_t size = kDefaultReadSize;

  if (read_hole_ != sparse_byte_buffer_.null_hole()) {
    // To serve the read request, we need to intake starting at the beginning
    // of read_hole_;
    FXL_DCHECK(read_request_);
    intake_hole_ = read_hole_;
    read_hole_ = sparse_byte_buffer_.null_hole();
    size = read_request_remaining_bytes_;
  } else if (intake_hole_ == sparse_byte_buffer_.null_hole()) {
    *size_out = 0;
    return kUnknownSize;
  }

  if (size > intake_hole_.size()) {
    size = intake_hole_.size();
  }

  *size_out = size;

  return intake_hole_.position();
}

void ReaderCache::Store::PutIntakeBuffer(size_t position,
                                         std::vector<uint8_t>&& buffer) {
  mutex_.lock();

  FXL_DCHECK(intake_hole_ != sparse_byte_buffer_.null_hole());
  FXL_DCHECK(position == intake_hole_.position());
  FXL_DCHECK(buffer.size() != 0);
  FXL_DCHECK(buffer.size() <= intake_hole_.size());

  if (read_hole_ != sparse_byte_buffer_.null_hole() &&
      read_hole_.position() >= position &&
      read_hole_.position() < position + buffer.size()) {
    // read_hole_ was set after the termination of GetIntakePositionAndSize
    // and before this point. We're in the process of delivering the requested
    // data, so we don't need read_hole_ to be set anymore.
    read_hole_ = sparse_byte_buffer_.null_hole();
  }

  intake_hole_ = sparse_byte_buffer_.Fill(intake_hole_, std::move(buffer));

  ServeRequest();  // unlocks mutex_
}

void ReaderCache::Store::ReportIntakeError(Result result) {
  FXL_DCHECK(result != Result::kOk);

  mutex_.lock();
  result_ = result;

  ServeRequest();  // unlocks mutex_
}

void ReaderCache::Store::ServeRequest() {
  ReadAtRequest* read_request_to_complete = nullptr;
  Result read_request_result;

  if (read_request_ == nullptr) {
    mutex_.unlock();
    return;
  }

  while (result_ == Result::kOk && read_request_remaining_bytes_ != 0u) {
    read_region_ = sparse_byte_buffer_.FindRegionContaining(
        read_request_position_, read_region_);
    if (read_region_ == sparse_byte_buffer_.null_region()) {
      // There's no region in the store for this position. Arrange for intake
      // to fill this need.
      read_hole_ = sparse_byte_buffer_.FindOrCreateHole(read_request_position_,
                                                        intake_hole_);
      mutex_.unlock();
      return;
    }

    // Perform the copy.
    FXL_DCHECK(read_region_.position() <= read_request_position_);
    FXL_DCHECK(read_region_.position() + read_region_.size() >
               read_request_position_);

    size_t bytes_to_copy = (read_region_.position() + read_region_.size()) -
                           read_request_position_;
    if (bytes_to_copy > read_request_remaining_bytes_) {
      bytes_to_copy = read_request_remaining_bytes_;
    }
    FXL_DCHECK(bytes_to_copy > 0);

    read_request_->CopyFrom(read_region_.data() + (read_request_position_ -
                                                   read_region_.position()),
                            bytes_to_copy);

    read_request_position_ += bytes_to_copy;
    read_request_remaining_bytes_ -= bytes_to_copy;
  }

  // Done with this request. Complete it.
  read_request_to_complete = read_request_;
  read_request_result = result_;
  read_request_ = nullptr;

  mutex_.unlock();

  FXL_DCHECK(read_request_to_complete);
  read_request_to_complete->Complete(read_request_result);
}

ReaderCache::Intake::Intake() {}

ReaderCache::Intake::~Intake() {}

void ReaderCache::Intake::Start(std::weak_ptr<ReaderCache> owner,
                                std::shared_ptr<Reader> upstream_reader) {
  FXL_DCHECK(upstream_reader);

  owner_ = owner;
  upstream_reader_ = upstream_reader;
  Continue();
}

void ReaderCache::Intake::Continue() {
  auto shared_owner = owner_.lock();
  FXL_DCHECK(shared_owner);

  size_t size;
  size_t position = shared_owner->store_.GetIntakePositionAndSize(&size);
  if (position == kUnknownSize) {
    return;
  }

  FXL_DCHECK(size > 0);

  FXL_DCHECK(buffer_.size() == 0);
  buffer_.resize(size);

  upstream_reader_->ReadAt(
      position, buffer_.data(), size,
      [this, weak_owner = owner_, position](Result result, size_t bytes_read) {
        auto shared_owner = weak_owner.lock();
        if (!shared_owner) {
          return;
        }

        if (result != Result::kOk) {
          FXL_LOG(ERROR) << "ReadAt failed";
          shared_owner->store_.ReportIntakeError(result);
          return;
        }

        FXL_DCHECK(bytes_read != 0);

        shared_owner->store_.PutIntakeBuffer(position, std::move(buffer_));
        FXL_DCHECK(buffer_.size() == 0);

        Continue();
      });
}

}  // namespace media_player
