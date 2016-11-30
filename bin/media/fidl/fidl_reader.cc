// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/fidl/fidl_reader.h"

#include <limits>
#include <string>

#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace media {

FidlReader::FidlReader(fidl::InterfaceHandle<SeekingReader> seeking_reader)
    : seeking_reader_(SeekingReaderPtr::Create(std::move(seeking_reader))) {
  task_runner_ = mtl::MessageLoop::GetCurrent()->task_runner();
  FTL_DCHECK(task_runner_);

  read_in_progress_ = false;

  seeking_reader_->Describe(
      [this](MediaResult result, uint64_t size, bool can_seek) {
        result_ = Convert(result);
        if (result_ == Result::kOk) {
          size_ = size;
          can_seek_ = can_seek;
        }
        ready_.Occur();
      });
}

FidlReader::~FidlReader() {}

void FidlReader::Describe(const DescribeCallback& callback) {
  ready_.When([this, callback]() { callback(result_, size_, can_seek_); });
}

void FidlReader::ReadAt(size_t position,
                        uint8_t* buffer,
                        size_t bytes_to_read,
                        const ReadAtCallback& callback) {
  FTL_DCHECK(buffer);
  FTL_DCHECK(bytes_to_read);

  FTL_DCHECK(!read_in_progress_)
      << "ReadAt called while previous call still in progress";
  read_in_progress_ = true;
  read_at_position_ = position;
  read_at_buffer_ = buffer;
  read_at_bytes_to_read_ = bytes_to_read;
  read_at_callback_ = callback;

  // ReadAt may be called on non-fidl threads, so we use the runner.
  task_runner_->PostTask([weak_this =
                              std::weak_ptr<FidlReader>(shared_from_this())]() {
    auto shared_this = weak_this.lock();
    if (shared_this) {
      shared_this->ContinueReadAt();
    }
  });
}

void FidlReader::ContinueReadAt() {
  ready_.When([this]() {
    if (result_ != Result::kOk) {
      CompleteReadAt(result_);
      return;
    }

    FTL_DCHECK(read_at_position_ < size_);

    if (read_at_position_ + read_at_bytes_to_read_ > size_) {
      read_at_bytes_to_read_ = size_ - read_at_position_;
    }

    read_at_bytes_remaining_ = read_at_bytes_to_read_;

    if (read_at_position_ == datapipe_consumer_position_) {
      ReadResponseBody();
      return;
    }

    datapipe_consumer_.reset();
    datapipe_consumer_position_ = kUnknownSize;

    if (!can_seek_ && read_at_position_ != 0) {
      CompleteReadAt(Result::kInvalidArgument);
      return;
    }

    seeking_reader_->ReadAt(
        read_at_position_,
        [this](MediaResult result, mx::datapipe_consumer datapipe_consumer) {
          result_ = Convert(result);
          if (result_ != Result::kOk) {
            CompleteReadAt(result_);
            return;
          }

          datapipe_consumer_ = std::move(datapipe_consumer);
          datapipe_consumer_position_ = read_at_position_;
          ReadResponseBody();
        });
  });
}

void FidlReader::ReadResponseBody() {
  FTL_DCHECK(read_at_bytes_remaining_ < std::numeric_limits<uint32_t>::max());
  mx_size_t byte_count;
  mx_status_t status = datapipe_consumer_.read(
      0u, read_at_buffer_, read_at_bytes_remaining_, &byte_count);

  if (status == ERR_SHOULD_WAIT) {
    byte_count = 0;
  } else if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::datapipe_consumer::read failed, status " << status;
    FailReadAt(status);
    return;
  }

  read_at_buffer_ += byte_count;
  read_at_bytes_remaining_ -= byte_count;
  datapipe_consumer_position_ += byte_count;

  if (read_at_bytes_remaining_ == 0) {
    CompleteReadAt(Result::kOk, read_at_bytes_to_read_);
    return;
  }

  fidl::GetDefaultAsyncWaiter()->AsyncWait(
      datapipe_consumer_.get(), MX_SIGNAL_READABLE, MX_TIME_INFINITE,
      FidlReader::ReadResponseBodyStatic, this);
}

void FidlReader::CompleteReadAt(Result result, size_t bytes_read) {
  ReadAtCallback read_at_callback;
  read_at_callback_.swap(read_at_callback);
  read_in_progress_ = false;
  read_at_callback(result, bytes_read);
}

void FidlReader::FailReadAt(mx_status_t status) {
  // TODO(dalesat): Expect some statuses here.
  FTL_DCHECK(false) << "Unexpected status " << status;
  result_ = Result::kUnknownError;
  datapipe_consumer_.reset();
  datapipe_consumer_position_ = kUnknownSize;
  CompleteReadAt(result_);
}

// static
void FidlReader::ReadResponseBodyStatic(mx_status_t status,
                                        mx_signals_t pending,
                                        void* closure) {
  FidlReader* reader = reinterpret_cast<FidlReader*>(closure);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "AsyncWait failed, status " << status;
    reader->FailReadAt(status);
    return;
  }

  reader->ReadResponseBody();
}

}  // namespace media
