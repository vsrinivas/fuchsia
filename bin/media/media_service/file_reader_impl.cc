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

namespace mojo {
namespace media {

// static
std::shared_ptr<FileReaderImpl> FileReaderImpl::Create(
    const String& path,
    InterfaceRequest<SeekingReader> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<FileReaderImpl>(
      new FileReaderImpl(path, request.Pass(), owner));
}

FileReaderImpl::FileReaderImpl(const String& path,
                               InterfaceRequest<SeekingReader> request,
                               MediaServiceImpl* owner)
    : MediaServiceImpl::Product<SeekingReader>(this, request.Pass(), owner),
      fd_(open(path.get().c_str(), O_RDONLY)),
      buffer_(kBufferSize) {
  result_ = fd_.is_valid() ? MediaResult::OK : MediaResult::NOT_FOUND;

  if (result_ == MediaResult::OK) {
    off_t seek_result = lseek(fd_.get(), 0, SEEK_END);
    if (seek_result >= 0) {
      size_ = static_cast<uint64_t>(seek_result);
    } else {
      // TODO(dalesat): More specific error code.
      size_ = kUnknownSize;
      result_ = MediaResult::UNKNOWN_ERROR;
    }
  }
}

FileReaderImpl::~FileReaderImpl() {}

void FileReaderImpl::Describe(const DescribeCallback& callback) {
  callback.Run(result_, size_, true);
}

void FileReaderImpl::ReadAt(uint64_t position, const ReadAtCallback& callback) {
  FTL_DCHECK(position < size_);

  if (result_ != MediaResult::OK) {
    callback.Run(result_, ScopedHandleBase<DataPipeConsumerHandle>());
    return;
  }

  if (producer_handle_.is_valid()) {
    FTL_DCHECK(wait_id_ != 0);
    Environment::GetDefaultAsyncWaiter()->CancelWait(wait_id_);
    wait_id_ = 0;
    producer_handle_.reset();
  }

  off_t seek_result = lseek(fd_.get(), position, SEEK_SET);
  if (seek_result < 0) {
    result_ = MediaResult::UNKNOWN_ERROR;
    callback.Run(result_, ScopedHandleBase<DataPipeConsumerHandle>());
    return;
  }

  ScopedDataPipeConsumerHandle consumer_handle;
  MojoResult result =
      CreateDataPipe(nullptr, &producer_handle_, &consumer_handle);
  if (result != MOJO_RESULT_OK) {
    // TODO(dalesat): More specific error code.
    result_ = MediaResult::UNKNOWN_ERROR;
    callback.Run(result_, ScopedHandleBase<DataPipeConsumerHandle>());
    return;
  }

  remaining_buffer_bytes_count_ = 0;
  reached_end_ = false;

  WriteToProducerHandle();

  if (result_ != MediaResult::OK) {
    // Error occurred during WriteToProducerHandle.
    callback.Run(result_, ScopedHandleBase<DataPipeConsumerHandle>());
    return;
  }

  callback.Run(result_, consumer_handle.Pass());
}

// static
void FileReaderImpl::WriteToProducerHandleStatic(void* reader_void_ptr,
                                                 MojoResult result) {
  FileReaderImpl* reader = reinterpret_cast<FileReaderImpl*>(reader_void_ptr);
  reader->wait_id_ = 0;
  if (result == MOJO_SYSTEM_RESULT_CANCELLED) {
    // Run loop has aborted...the app is shutting down.
    reader->producer_handle_.reset();
    return;
  }

  if (result != MOJO_RESULT_OK) {
    reader->producer_handle_.reset();
    return;
  }

  reader->WriteToProducerHandle();
}

void FileReaderImpl::WriteToProducerHandle() {
  while (true) {
    if (remaining_buffer_bytes_count_ == 0 && !reached_end_) {
      ReadFromFile();
    }

    if (remaining_buffer_bytes_count_ == 0) {
      producer_handle_.reset();
      return;
    }

    FTL_DCHECK(remaining_buffer_bytes_ != nullptr);

    uint32_t byte_count = remaining_buffer_bytes_count_;
    MojoResult result =
        WriteDataRaw(producer_handle_.get(), remaining_buffer_bytes_,
                     &byte_count, MOJO_WRITE_DATA_FLAG_NONE);
    if (result == MOJO_RESULT_OK) {
      FTL_DCHECK(byte_count != 0);
      remaining_buffer_bytes_ += byte_count;
      remaining_buffer_bytes_count_ -= byte_count;
      continue;
    }

    if (result == MOJO_SYSTEM_RESULT_SHOULD_WAIT) {
      wait_id_ = Environment::GetDefaultAsyncWaiter()->AsyncWait(
          producer_handle_.get().value(), MOJO_HANDLE_SIGNAL_WRITABLE,
          MOJO_DEADLINE_INDEFINITE, FileReaderImpl::WriteToProducerHandleStatic,
          this);

      return;
    }

    // TODO(dalesat): Remove UNKNOWN when fix lands for
    // https://fuchsia.atlassian.net/projects/US/issues/US-43.
    if (result == MOJO_SYSTEM_RESULT_FAILED_PRECONDITION ||
        result == MOJO_SYSTEM_RESULT_UNKNOWN) {
      // Consumer end was closed. This is normal behavior, depending on what
      // the consumer is up to.
      producer_handle_.reset();
      return;
    }

    FTL_DCHECK(false) << "WriteDataRaw failed, " << result;
    producer_handle_.reset();
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
}  // namespace mojo
