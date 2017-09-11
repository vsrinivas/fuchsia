// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zip/zipper.h"

#include <string.h>

#include <utility>

#include "lib/zip/memory_io.h"
#include "lib/fxl/logging.h"
#include "third_party/zlib/contrib/minizip/zip.h"

namespace zip {

Zipper::Zipper() {
  zlib_filefunc_def io = internal::kMemoryIO;
  io.opaque = &buffer_;
  encoder_.reset(zipOpen2(nullptr, APPEND_STATUS_CREATE, nullptr, &io));
}

Zipper::~Zipper() {}

bool Zipper::AddCompressedFile(const std::string& path,
                               const char* data,
                               size_t size) {
  FXL_DCHECK(encoder_.is_valid());

  zip_fileinfo file_info;
  memset(&file_info, 0, sizeof(file_info));
  int result = zipOpenNewFileInZip(encoder_.get(), path.c_str(), &file_info,
                                   nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED,
                                   Z_DEFAULT_COMPRESSION);
  if (result != ZIP_OK) {
    FXL_LOG(WARNING) << "Unable to create '" << path << "' in archive.";
    return false;
  }

  result = zipWriteInFileInZip(encoder_.get(), data, size);

  if (result < 0) {
    FXL_LOG(WARNING) << "Unable to write data into '" << path
                     << "' in archive.";
    return false;
  }

  result = zipCloseFileInZip(encoder_.get());

  if (result != ZIP_OK) {
    FXL_LOG(WARNING) << "Unable to close '" << path << "' in archive.";
    return false;
  }

  return true;
}

std::vector<char> Zipper::Finish() {
  encoder_.reset();
  return std::move(buffer_);
}

}  // namespace zip
