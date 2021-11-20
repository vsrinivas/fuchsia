// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_READER_FILE_READER_H_
#define SRC_MEDIA_VNEXT_LIB_READER_FILE_READER_H_

#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>

#include <fbl/unique_fd.h>

#include "src/media/vnext/lib/reader/reader.h"

namespace fmlib {

// Reads from a file on behalf of a demux.
class FileReader : public Reader {
 public:
  static std::shared_ptr<FileReader> Create(zx::channel file_channel);

  explicit FileReader(fbl::unique_fd fd);

  ~FileReader() override = default;

  // Reader implementation.
  void Describe(DescribeCallback callback) override;

  void ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
              ReadAtCallback callback) override;

 private:
  async_dispatcher_t* dispatcher_;
  fbl::unique_fd fd_;
  zx_status_t status_ = ZX_OK;
  uint64_t size_ = kUnknownSize;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_READER_FILE_READER_H_
