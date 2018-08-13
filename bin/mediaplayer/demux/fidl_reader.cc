// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/demux/fidl_reader.h"

#include <limits>
#include <string>

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/bin/mediaplayer/fidl/fidl_type_conversions.h"
#include "lib/fxl/logging.h"

namespace media_player {

FidlReader::FidlReader(
    fidl::InterfaceHandle<fuchsia::mediaplayer::SeekingReader> seeking_reader)
    : seeking_reader_(seeking_reader.Bind()),
      dispatcher_(async_get_default_dispatcher()),
      ready_(dispatcher_) {
  FXL_DCHECK(dispatcher_);

  read_in_progress_ = false;

  seeking_reader_->Describe([this](fuchsia::mediaplayer::MediaResult result,
                                   uint64_t size, bool can_seek) {
    result_ = fxl::To<Result>(result);
    if (result_ == Result::kOk) {
      size_ = size;
      can_seek_ = can_seek;
    }
    ready_.Occur();
  });
}

FidlReader::~FidlReader() {}

void FidlReader::Describe(DescribeCallback callback) {
  ready_.When([this, callback = std::move(callback)]() {
    callback(result_, size_, can_seek_);
  });
}

void FidlReader::ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
                        ReadAtCallback callback) {
  FXL_DCHECK(buffer);
  FXL_DCHECK(bytes_to_read);

  FXL_DCHECK(!read_in_progress_)
      << "ReadAt called while previous call still in progress";
  read_in_progress_ = true;
  read_at_position_ = position;
  read_at_buffer_ = buffer;
  read_at_bytes_to_read_ = bytes_to_read;
  read_at_callback_ = std::move(callback);

  // ReadAt may be called on non-fidl threads, so we use the runner.
  async::PostTask(
      dispatcher_,
      [weak_this = std::weak_ptr<FidlReader>(shared_from_this())]() {
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
      FXL_DCHECK(socket_);
      ReadFromSocket();
      return;
    }

    socket_.reset();
    socket_position_ = kUnknownSize;

    if (!can_seek_ && read_at_position_ != 0) {
      CompleteReadAt(Result::kInvalidArgument);
      return;
    }

    seeking_reader_->ReadAt(
        read_at_position_,
        [this](fuchsia::mediaplayer::MediaResult result, zx::socket socket) {
          result_ = fxl::To<Result>(result);
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
      waiter_ = std::make_unique<async::Wait>(
          socket_.get(), ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);

      waiter_->set_handler([this](async_dispatcher_t* dispatcher,
                                  async::Wait* wait, zx_status_t status,
                                  const zx_packet_signal_t* signal) {
        if (status != ZX_OK) {
          if (status != ZX_ERR_CANCELED) {
            FXL_LOG(ERROR) << "Wait failed, status " << status;
          }

          FailReadAt(status);
          return;
        }

        ReadFromSocket();
      });

      waiter_->Begin(async_get_default_dispatcher());

      break;
    }

    waiter_.reset();

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
      result_ = Result::kPeerClosed;
      break;
    case ZX_ERR_CANCELED:
      result_ = Result::kCancelled;
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

}  // namespace media_player
