// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <mx/datapipe.h>

#include "apps/media/services/seeking_reader.fidl.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"

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
  static constexpr mx_size_t kBufferSize = 8192;
  static constexpr mx_size_t kDatapipeCapacity = 65536;

  FileReaderImpl(const fidl::String& path,
                 fidl::InterfaceRequest<SeekingReader> request,
                 MediaServiceImpl* owner);

  // Callback function for WriteToProducer's async wait.
  static void WriteToProducerStatic(mx_status_t status,
                                    mx_signals_t pending,
                                    void* closure);

  // Writes data to producer_handle_;
  void WriteToProducer();

  // Reads from the file into buffer_;
  void ReadFromFile();

  ftl::UniqueFD fd_;
  MediaResult result_ = MediaResult::OK;
  uint64_t size_ = kUnknownSize;
  mx::datapipe_producer datapipe_producer_;
  FidlAsyncWaitID wait_id_ = 0;
  std::vector<char> buffer_;
  mx_size_t remaining_buffer_bytes_count_ = 0;
  char* remaining_buffer_bytes_;
  bool reached_end_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FileReaderImpl);
};

}  // namespace media
