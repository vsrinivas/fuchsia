// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <zx/socket.h>

#include "garnet/bin/media/media_service/media_service_impl.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"
#include "lib/media/fidl/seeking_reader.fidl.h"

namespace media {

// Fidl agent that reads from a file.
class FileReaderImpl : public MediaServiceImpl::Product<SeekingReader>,
                       public SeekingReader {
 public:
  static std::shared_ptr<FileReaderImpl> Create(
      const fidl::String& path,
      fidl::InterfaceRequest<SeekingReader> request,
      MediaServiceImpl* owner);

  ~FileReaderImpl() override;

  // SeekingReader implementation.
  void Describe(const DescribeCallback& callback) override;

  void ReadAt(uint64_t position, const ReadAtCallback& callback) override;

 private:
  static constexpr size_t kBufferSize = 8192;

  FileReaderImpl(const fidl::String& path,
                 fidl::InterfaceRequest<SeekingReader> request,
                 MediaServiceImpl* owner);

  // Callback function for WriteToSocket's async wait.
  static void WriteToSocketStatic(zx_status_t status,
                                  zx_signals_t pending,
                                  uint64_t count,
                                  void* closure);

  // Writes data to socket_;
  void WriteToSocket();

  // Reads from the file into buffer_;
  void ReadFromFile();

  fxl::UniqueFD fd_;
  MediaResult result_ = MediaResult::OK;
  uint64_t size_ = kUnknownSize;
  zx::socket socket_;
  FidlAsyncWaitID wait_id_ = 0;
  std::vector<char> buffer_;
  size_t remaining_buffer_bytes_count_ = 0;
  char* remaining_buffer_bytes_;
  bool reached_end_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FileReaderImpl);
};

}  // namespace media
