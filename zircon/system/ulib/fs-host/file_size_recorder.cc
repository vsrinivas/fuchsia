// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-host/file_size_recorder.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

FileSizeRecorder::FileSizeRecorder() = default;
FileSizeRecorder::~FileSizeRecorder() = default;

bool FileSizeRecorder::OpenSizeFile(const char* const path) {
  std::lock_guard<std::mutex> lock(sizes_file_lock_);
  if (sizes_file_)
    return false;
  sizes_file_.reset(open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644));
  return bool{sizes_file_};
}

bool FileSizeRecorder::AppendSizeInformation(const char* const name, size_t size) {
  std::lock_guard<std::mutex> lock(sizes_file_lock_);

  if (!sizes_file_) {
    return true;
  }

  if (dprintf(sizes_file_.get(), "%s=%zu\n", name, size) < 0) {
    fprintf(stderr, "error: sizes file append error\n");
    return false;
  }
  return true;
}
