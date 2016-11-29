// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/file_reader_impl.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace media {

// static
std::shared_ptr<FileReaderImpl> FileReaderImpl::Create(
    const fidl::String& path,
    fidl::InterfaceRequest<SeekingReader> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<FileReaderImpl>(
      new FileReaderImpl(path, std::move(request), owner));
}

FileReaderImpl::FileReaderImpl(const fidl::String& path,
                               fidl::InterfaceRequest<SeekingReader> request,
                               MediaServiceImpl* owner)
    : MediaServiceImpl::Product<SeekingReader>(this, std::move(request), owner),
      fd_(open(path.get().c_str(), O_RDONLY)),
      buffer_(kBufferSize) {
  result_ = fd_.is_valid() ? MediaResult::OK : MediaResult::NOT_FOUND;

  if (result_ == MediaResult::OK) {
    off_t seek_result = lseek(fd_.get(), 0, SEEK_END);
    if (seek_result >= 0) {
      size_ = static_cast<uint64_t>(seek_result);
    } else {
      size_ = kUnknownSize;
      // TODO(dalesat): More specific error code.
      result_ = MediaResult::UNKNOWN_ERROR;
    }
  }
}

FileReaderImpl::~FileReaderImpl() {}

void FileReaderImpl::Describe(const DescribeCallback& callback) {
  callback(result_, size_, true);
}

void FileReaderImpl::ReadAt(uint64_t position, const ReadAtCallback& callback) {
  FTL_DCHECK(position < size_);

  if (result_ != MediaResult::OK) {
    callback(result_, mx::datapipe_consumer());
    return;
  }

  if (datapipe_producer_) {
    FTL_DCHECK(wait_id_ != 0);
    fidl::GetDefaultAsyncWaiter()->CancelWait(wait_id_);
    wait_id_ = 0;
    datapipe_producer_.reset();
  }

  off_t seek_result = lseek(fd_.get(), position, SEEK_SET);
  if (seek_result < 0) {
    // TODO(dalesat): More specific error code.
    result_ = MediaResult::UNKNOWN_ERROR;
    callback(result_, mx::datapipe_consumer());
    return;
  }

  mx::datapipe_consumer datapipe_consumer;
  mx_status_t status = mx::datapipe<void>::create(
      1u, kDatapipeCapacity, 0u, &datapipe_producer_, &datapipe_consumer);
  if (status != NO_ERROR) {
    // TODO(dalesat): More specific error code.
    result_ = MediaResult::UNKNOWN_ERROR;
    callback(result_, mx::datapipe_consumer());
    return;
  }

  remaining_buffer_bytes_count_ = 0;
  reached_end_ = false;

  WriteToProducer();

  if (result_ != MediaResult::OK) {
    // Error occurred during WriteToProducer.
    callback(result_, mx::datapipe_consumer());
    return;
  }

  callback(result_, std::move(datapipe_consumer));
}

// static
void FileReaderImpl::WriteToProducerStatic(mx_status_t status,
                                           mx_signals_t pending,
                                           void* closure) {
  FileReaderImpl* reader = reinterpret_cast<FileReaderImpl*>(closure);
  reader->wait_id_ = 0;
  if (status == ERR_BAD_STATE) {
    // Run loop has aborted...the app is shutting down.
    reader->datapipe_producer_.reset();
    return;
  }

  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::datapipe_producer::write failed, status " << status;
    reader->datapipe_producer_.reset();
    return;
  }

  reader->WriteToProducer();
}

void FileReaderImpl::WriteToProducer() {
  while (true) {
    if (remaining_buffer_bytes_count_ == 0 && !reached_end_) {
      ReadFromFile();
    }

    if (remaining_buffer_bytes_count_ == 0) {
      datapipe_producer_.reset();
      return;
    }

    FTL_DCHECK(remaining_buffer_bytes_ != nullptr);

    mx_size_t byte_count;
    mx_status_t status =
        datapipe_producer_.write(0u, remaining_buffer_bytes_,
                                 remaining_buffer_bytes_count_, &byte_count);

    if (status == NO_ERROR) {
      FTL_DCHECK(byte_count != 0);
      remaining_buffer_bytes_ += byte_count;
      remaining_buffer_bytes_count_ -= byte_count;
      continue;
    }

    if (status == ERR_SHOULD_WAIT) {
      wait_id_ = fidl::GetDefaultAsyncWaiter()->AsyncWait(
          datapipe_producer_.get(), MX_SIGNAL_WRITABLE, MX_TIME_INFINITE,
          FileReaderImpl::WriteToProducerStatic, this);
      return;
    }

    if (status == ERR_REMOTE_CLOSED) {
      // Consumer end was closed. This is normal behavior, depending on what
      // the consumer is up to.
      datapipe_producer_.reset();
      return;
    }

    FTL_LOG(ERROR) << "mx::datapipe_producer::write failed, status " << status;
    datapipe_producer_.reset();
    // TODO(dalesat): More specific error code.
    result_ = MediaResult::UNKNOWN_ERROR;
    return;
  }
}

void FileReaderImpl::ReadFromFile() {
  FTL_DCHECK(buffer_.size() == kBufferSize);
  FTL_DCHECK(!reached_end_);

  ssize_t result =
      ftl::ReadFileDescriptor(fd_.get(), buffer_.data(), kBufferSize);
  if (result < 0) {
    // TODO(dalesat): More specific error code.
    result_ = MediaResult::UNKNOWN_ERROR;
    return;
  }

  if (result < static_cast<ssize_t>(kBufferSize)) {
    reached_end_ = true;
  }

  remaining_buffer_bytes_count_ = static_cast<size_t>(result);
  remaining_buffer_bytes_ = buffer_.data();
}

}  // namespace media
