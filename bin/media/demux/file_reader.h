// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/channel.h>

#include "garnet/bin/media/demux/reader.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/tasks/task_runner.h"

namespace media {

// Reads from a file on behalf of a demux.
class FileReader : public Reader {
 public:
  static std::shared_ptr<FileReader> Create(zx::channel file_channel);

  FileReader(fxl::UniqueFD fd);

  ~FileReader() override;

  // Reader implementation.
  void Describe(DescribeCallback callback) override;

  void ReadAt(size_t position,
              uint8_t* buffer,
              size_t bytes_to_read,
              ReadAtCallback callback) override;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  fxl::UniqueFD fd_;
  Result result_ = Result::kOk;
  uint64_t size_ = kUnknownSize;
};

}  // namespace media
