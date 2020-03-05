// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/rotating_file_set.h"

#include <assert.h>
#include <sys/stat.h>

#include <fstream>
#include <sstream>

#include <trace/event.h>

namespace feedback {

RotatingFileSetWriter::RotatingFileSetWriter(const std::vector<const std::string>& file_paths,
                                             FileSize total_size)
    : file_paths_(file_paths),
      individual_file_size_(total_size / file_paths.size()),
      current_file_(individual_file_size_) {
  assert(file_paths_.size() > 0 && "|file_paths_| must have non-zero size");

  // This will truncate the file.
  current_file_.Open(file_paths_.front());
}

void RotatingFileSetWriter::Write(const std::string& line) {
  TRACE_DURATION("feedback:io", "RotatingFileSetWriter::Write", "line_size", line.size());

  if (individual_file_size_.to_bytes() < line.size()) {
    return;
  }

  if (current_file_.BytesRemaining() < line.size()) {
    current_file_.Close();
    RotateFilePaths();

    // This re-creates the first file in the list.
    current_file_.Open(file_paths_.front());
  }
  current_file_.Write(line);
}

void RotatingFileSetWriter::RotateFilePaths() {
  TRACE_DURATION("feedback:io", "RotatingFileSetWriter::RotateFilePaths");

  // Assuming we have 4 files file0.txt, file1.txt, file2.txt, and file3.txt, in that order, their
  // names will change as follows:
  // files2.txt -> file3.txt, file1.txt -> file2.txt, file0.txt -> file1.txt.
  // The contents of file3.txt no longer exist.
  for (size_t i = file_paths_.size() - 1; i > 0; --i) {
    rename(file_paths_[i - 1].c_str(), file_paths_[i].c_str());
  }
}

RotatingFileSetReader::RotatingFileSetReader(const std::vector<const std::string>& file_paths)
    : file_paths_(file_paths) {}

namespace {

size_t GetFileSize(const std::string& file_path) {
  struct stat file_status;
  file_status.st_size = 0;
  stat(file_path.c_str(), &file_status);
  return file_status.st_size;
}

}  // namespace

bool RotatingFileSetReader::Concatenate(const std::string& file_path) const {
  size_t total_bytes = 0;
  for (auto path = file_paths_.crbegin(); path != file_paths_.crend(); ++path) {
    total_bytes += GetFileSize(*path);
  }

  if (total_bytes == 0) {
    return false;
  }

  std::ofstream out(file_path, std::iostream::out | std::iostream::trunc);
  if (!out.is_open()) {
    return false;
  }

  std::ifstream in;
  for (auto path = file_paths_.crbegin(); path != file_paths_.crend(); ++path) {
    in.open(path->c_str());
    if (in.is_open()) {
      out << in.rdbuf();
    }
    in.close();
  }

  out.flush();
  out.close();

  return true;
}

}  // namespace feedback
