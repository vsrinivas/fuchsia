// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/demux/file_reader.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <unistd.h>

#include "garnet/bin/media/util/file_channel.h"
#include "lib/fxl/files/file_descriptor.h"

namespace media_player {

// static
std::shared_ptr<FileReader> FileReader::Create(zx::channel file_channel) {
  return std::make_shared<FileReader>(
      media::FdFromChannel(std::move(file_channel)));
}

FileReader::FileReader(fxl::UniqueFD fd)
    : async_(async_get_default()), fd_(std::move(fd)) {
  FXL_DCHECK(async_);

  result_ = fd_.is_valid() ? Result::kOk : Result::kNotFound;

  if (result_ == Result::kOk) {
    off_t seek_result = lseek(fd_.get(), 0, SEEK_END);
    if (seek_result >= 0) {
      size_ = static_cast<uint64_t>(seek_result);
    } else {
      size_ = kUnknownSize;
      // TODO(dalesat): More specific error code.
      result_ = Result::kUnknownError;
    }
  }
}

FileReader::~FileReader() {}

void FileReader::Describe(DescribeCallback callback) {
  callback(result_, size_, true);
}

void FileReader::ReadAt(size_t position,
                        uint8_t* buffer,
                        size_t bytes_to_read,
                        ReadAtCallback callback) {
  FXL_DCHECK(position < size_);

  if (result_ != Result::kOk) {
    callback(result_, 0);
    return;
  }

  off_t seek_result = lseek(fd_.get(), position, SEEK_SET);
  if (seek_result < 0) {
    FXL_LOG(ERROR) << "seek failed, result " << seek_result << " errno "
                   << errno;
    // TODO(dalesat): More specific error code.
    result_ = Result::kUnknownError;
    callback(result_, 0);
    return;
  }

  ssize_t result = fxl::ReadFileDescriptor(
      fd_.get(), reinterpret_cast<char*>(buffer), bytes_to_read);
  if (result < 0) {
    // TODO(dalesat): More specific error code.
    result_ = Result::kUnknownError;
    callback(result_, 0);
    return;
  }

  async::PostTask(async_, [callback = std::move(callback), result]() {
    callback(Result::kOk, result);
  });
}

}  // namespace media_player
