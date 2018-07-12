// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_DEMUX_FILE_READER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_DEMUX_FILE_READER_H_

#include <lib/async/dispatcher.h>
#include <zx/channel.h>

#include "garnet/bin/media/media_player/demux/reader.h"
#include "lib/fxl/files/unique_fd.h"

namespace media_player {

// Reads from a file on behalf of a demux.
class FileReader : public Reader {
 public:
  static std::shared_ptr<FileReader> Create(zx::channel file_channel);

  FileReader(fxl::UniqueFD fd);

  ~FileReader() override;

  // Reader implementation.
  void Describe(DescribeCallback callback) override;

  void ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
              ReadAtCallback callback) override;

 private:
  async_dispatcher_t* dispatcher_;
  fxl::UniqueFD fd_;
  Result result_ = Result::kOk;
  uint64_t size_ = kUnknownSize;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_DEMUX_FILE_READER_H_
