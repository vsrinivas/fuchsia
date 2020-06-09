// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/system_log_recorder/reader.h"

#include <assert.h>
#include <lib/trace/event.h>
#include <sys/stat.h>

#include <fstream>
#include <sstream>

namespace feedback {

namespace {

size_t GetFileSize(const std::string& file_path) {
  struct stat file_status;
  file_status.st_size = 0;
  stat(file_path.c_str(), &file_status);
  return file_status.st_size;
}

}  // namespace

bool Concatenate(const std::vector<const std::string>& input_file_paths,
                 const std::string& output_file_path) {
  size_t total_bytes = 0;
  for (auto path = input_file_paths.crbegin(); path != input_file_paths.crend(); ++path) {
    total_bytes += GetFileSize(*path);
  }

  if (total_bytes == 0) {
    return false;
  }

  std::ofstream out(output_file_path, std::iostream::out | std::iostream::trunc);
  if (!out.is_open()) {
    return false;
  }

  std::ifstream in;
  for (auto path = input_file_paths.crbegin(); path != input_file_paths.crend(); ++path) {
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
