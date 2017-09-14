// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/fidl/fidl_reader.h"

#include <limits>
#include <string>

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

namespace media {

FidlReader::FidlReader(fidl::InterfaceHandle<SeekingReader> seeking_reader)
    : seeking_reader_(SeekingReaderPtr::Create(std::move(seeking_reader))) {
  task_runner_ = fsl::MessageLoop::GetCurrent()->task_runner();
  FXL_DCHECK(task_runner_);

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

FidlReader::~FidlReader() {
  if (wait_id_ != 0) {
    fidl::GetDefaultAsyncWaiter()->CancelWait(wait_id_);
  }
}

void FidlReader::Describe(const DescribeCallback& callback) {
  ready_.When([this, callback]() { callback(result_, size_, can_seek_); });
}

void FidlReader::ReadAt(size_t position,
                        uint8_t* buffer,
                        size_t bytes_to_read,
                        const ReadAtCallback& callback) {
  FXL_DCHECK(buffer);
  FXL_DCHECK(bytes_to_read);

  FXL_DCHECK(!read_in_progress_)
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

    FXL_DCHECK(read_at_position_ < size_);

    if (read_at_position_ + read_at_bytes_to_read_ > size_) {
      read_at_bytes_to_read_ = size_ - read_at_position_;
    }

    read_at_bytes_remaining_ = read_at_bytes_to_read_;

    if (read_at_position_ == socket_position_) {
      ReadFromSocket();
      return;
    }

    socket_.reset();
    socket_position_ = kUnknownSize;

    if (!can_seek_ && read_at_position_ != 0) {
      CompleteReadAt(Result::kInvalidArgument);
      return;
    }

    seeking_reader_->ReadAt(read_at_position_,
                            [this](MediaResult result, zx::socket socket) {
                              result_ = Convert(result);
                              if (result_ != Result::kOk) {
                                CompleteReadAt(result_);
                                return;
                              }

                              socket_ = std::move(socket);
                              socket_position_ = read_at_position_;
                              ReadFromSocket();
                            });
  });
}

void FidlReader::ReadFromSocket() {
  while (true) {
    FXL_DCHECK(read_at_bytes_remaining_ < std::numeric_limits<uint32_t>::max());
    size_t byte_count = 0;
    zx_status_t status = socket_.read(0u, read_at_buffer_,
                                      read_at_bytes_remaining_, &byte_count);

    if (status == ZX_ERR_SHOULD_WAIT) {
      wait_id_ = fidl::GetDefaultAsyncWaiter()->AsyncWait(
          socket_.get(), ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
          ZX_TIME_INFINITE, FidlReader::ReadFromSocketStatic, this);
      break;
    }

    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::socket::read failed, status " << status;
      FailReadAt(status);
      break;
    }

    read_at_buffer_ += byte_count;
    read_at_bytes_remaining_ -= byte_count;
    socket_position_ += byte_count;

    if (read_at_bytes_remaining_ == 0) {
      CompleteReadAt(Result::kOk, read_at_bytes_to_read_);
      break;
    }
  }
}

void FidlReader::CompleteReadAt(Result result, size_t bytes_read) {
  ReadAtCallback read_at_callback;
  read_at_callback_.swap(read_at_callback);
  read_in_progress_ = false;
  read_at_callback(result, bytes_read);
}

void FidlReader::FailReadAt(zx_status_t status) {
  switch (status) {
    case ZX_ERR_PEER_CLOSED:
      result_ = Result::kInternalError;
      break;
    // TODO(dalesat): Expect more statuses here.
    default:
      FXL_LOG(ERROR) << "Unexpected status " << status;
      result_ = Result::kUnknownError;
      break;
  }

  socket_.reset();
  socket_position_ = kUnknownSize;
  CompleteReadAt(result_);
}

// static
void FidlReader::ReadFromSocketStatic(zx_status_t status,
                                      zx_signals_t pending,
                                      uint64_t count,
                                      void* closure) {
  FidlReader* reader = reinterpret_cast<FidlReader*>(closure);

  reader->wait_id_ = 0;

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "AsyncWait failed, status " << status;
    reader->FailReadAt(status);
    return;
  }

  reader->ReadFromSocket();
}

}  // namespace media
