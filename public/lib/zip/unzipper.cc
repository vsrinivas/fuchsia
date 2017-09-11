// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zip/unzipper.h"

#include <stdio.h>

#include <utility>

#include "lib/fxl/logging.h"
#include "lib/zip/create_unzipper.h"
#include "third_party/zlib/contrib/minizip/unzip.h"

namespace zip {

Unzipper::Unzipper(std::vector<char> buffer)
    : buffer_(std::move(buffer)), decoder_(CreateUnzipper(&buffer_)) {}

Unzipper::~Unzipper() {}

std::vector<char> Unzipper::Extract(const std::string& path) {
  std::vector<char> buffer;

  int result = unzLocateFile(decoder_.get(), path.c_str(), 0);
  if (result != UNZ_OK) {
    FXL_LOG(WARNING) << "Unable to locate '" << path << "' in archive.";
    return buffer;
  }

  result = unzOpenCurrentFile(decoder_.get());
  if (result != UNZ_OK) {
    FXL_LOG(WARNING) << "unzOpenCurrentFile failed, error=" << result;
    return buffer;
  }

  unz_file_info file_info;
  result = unzGetCurrentFileInfo(decoder_.get(), &file_info, nullptr, 0,
                                 nullptr, 0, nullptr, 0);
  if (result != UNZ_OK) {
    FXL_LOG(WARNING) << "unzGetCurrentFileInfo failed, error=" << result;
    return buffer;
  }

  buffer.resize(file_info.uncompressed_size);

  result = unzReadCurrentFile(decoder_.get(), buffer.data(), buffer.size());
  if (result < 0 || static_cast<size_t>(result) != buffer.size()) {
    FXL_LOG(WARNING) << "Unzip failed, error=" << result;
    buffer.clear();
    return buffer;
  }

  return buffer;
}

}  // namespace zip
