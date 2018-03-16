// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fuchsia/cpp/media.h>
#include <lib/async/cpp/auto_wait.h>
#include <lib/zx/socket.h>

#include "garnet/bin/media/media_service/media_component_factory.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"

namespace media {

// Fidl agent that reads from a file.
class FileReaderImpl : public MediaComponentFactory::Product<SeekingReader>,
                       public SeekingReader {
 public:
  static std::shared_ptr<FileReaderImpl> Create(
      zx::channel file_channel,
      fidl::InterfaceRequest<SeekingReader> request,
      MediaComponentFactory* owner);

  ~FileReaderImpl() override;

  // SeekingReader implementation.
  void Describe(DescribeCallback callback) override;

  void ReadAt(uint64_t position, ReadAtCallback callback) override;

 private:
  static constexpr size_t kBufferSize = 8192;

  FileReaderImpl(fxl::UniqueFD fd,
                 fidl::InterfaceRequest<SeekingReader> request,
                 MediaComponentFactory* owner);

  // Writes data to socket_;
  void WriteToSocket();

  // Reads from the file into buffer_;
  void ReadFromFile();

  fxl::UniqueFD fd_;
  MediaResult result_ = MediaResult::OK;
  uint64_t size_ = kUnknownSize;
  zx::socket socket_;
  std::unique_ptr<async::AutoWait> waiter_;
  std::vector<char> buffer_;
  size_t remaining_buffer_bytes_count_ = 0;
  char* remaining_buffer_bytes_;
  bool reached_end_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FileReaderImpl);
};

}  // namespace media
