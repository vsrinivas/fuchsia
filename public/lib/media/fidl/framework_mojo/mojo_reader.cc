// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework_mojo/mojo_reader.h"

#include <mojo/system/result.h>

#include <limits>
#include <string>

#include "apps/media/services/framework_mojo/mojo_type_conversions.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mojo {
namespace media {

MojoReader::MojoReader(InterfaceHandle<SeekingReader> seeking_reader)
    : seeking_reader_(SeekingReaderPtr::Create(seeking_reader.Pass())) {
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

MojoReader::~MojoReader() {}

void MojoReader::Describe(const DescribeCallback& callback) {
  ready_.When([this, callback]() { callback(result_, size_, can_seek_); });
}

void MojoReader::ReadAt(size_t position,
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

  // ReadAt may be called on non-mojo threads, so we use the runner.
  task_runner_->PostTask([this]() { ContinueReadAt(); });
}

void MojoReader::ContinueReadAt() {
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

    if (read_at_position_ == consumer_handle_position_) {
      ReadResponseBody();
      return;
    }

    consumer_handle_.reset();
    consumer_handle_position_ = kUnknownSize;

    if (!can_seek_ && read_at_position_ != 0) {
      CompleteReadAt(Result::kInvalidArgument);
      return;
    }

    seeking_reader_->ReadAt(
        read_at_position_,
        [this](MediaResult result,
               ScopedDataPipeConsumerHandle consumer_handle) {
          result_ = Convert(result);
          if (result_ != Result::kOk) {
            CompleteReadAt(result_);
            return;
          }

          consumer_handle_ = consumer_handle.Pass();
          consumer_handle_position_ = read_at_position_;
          ReadResponseBody();
        });
  });
}

void MojoReader::ReadResponseBody() {
  FTL_DCHECK(read_at_bytes_remaining_ < std::numeric_limits<uint32_t>::max());
  uint32_t byte_count = static_cast<uint32_t>(read_at_bytes_remaining_);
  MojoResult result = ReadDataRaw(consumer_handle_.get(), read_at_buffer_,
                                  &byte_count, MOJO_READ_DATA_FLAG_NONE);

  if (result == MOJO_SYSTEM_RESULT_SHOULD_WAIT) {
    byte_count = 0;
  } else if (result != MOJO_RESULT_OK) {
    FTL_LOG(ERROR) << "ReadDataRaw failed " << result;
    FailReadAt(result);
    return;
  }

  read_at_buffer_ += byte_count;
  read_at_bytes_remaining_ -= byte_count;
  consumer_handle_position_ += byte_count;

  if (read_at_bytes_remaining_ == 0) {
    CompleteReadAt(Result::kOk, read_at_bytes_to_read_);
    return;
  }

  Environment::GetDefaultAsyncWaiter()->AsyncWait(
      consumer_handle_.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
      MOJO_DEADLINE_INDEFINITE, MojoReader::ReadResponseBodyStatic, this);
}

void MojoReader::CompleteReadAt(Result result, size_t bytes_read) {
  ReadAtCallback read_at_callback;
  read_at_callback_.swap(read_at_callback);
  read_in_progress_ = false;
  read_at_callback(result, bytes_read);
}

void MojoReader::FailReadAt(MojoResult result) {
  result_ = ConvertResult(result);
  consumer_handle_.reset();
  consumer_handle_position_ = kUnknownSize;
  CompleteReadAt(result_);
}

// static
void MojoReader::ReadResponseBodyStatic(void* reader_void_ptr,
                                        MojoResult result) {
  MojoReader* reader = reinterpret_cast<MojoReader*>(reader_void_ptr);
  if (result != MOJO_RESULT_OK) {
    FTL_LOG(ERROR) << "AsyncWait failed " << result;
    reader->FailReadAt(result);
    return;
  }

  reader->ReadResponseBody();
}

}  // namespace media
}  // namespace mojo
