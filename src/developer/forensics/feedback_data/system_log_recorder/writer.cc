// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/writer.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <unistd.h>

namespace forensics {
namespace feedback_data {

SystemLogWriter::SystemLogWriter(const std::vector<const std::string>& file_paths,
                                 LogMessageStore* store)
    : file_paths_(file_paths), store_(store) {
  FX_CHECK(file_paths_.size() > 0);
  StartNewFile();
}

void SystemLogWriter::StartNewFile() {
  if (current_file_descriptor_ >= 0) {
    close(current_file_descriptor_);
    current_file_descriptor_ = -1;
  }

  RotateFilePaths();

  TRACE_DURATION("feedback:io", "SystemLogWriter::OpenFile");
  current_file_descriptor_ = open(file_paths_.front().c_str(), O_WRONLY | O_CREAT | O_TRUNC);
}

void SystemLogWriter::Write() {
  TRACE_DURATION("feedback:io", "SystemLogWriter::Write");
  bool end_of_block;
  const std::string str = store_->Consume(&end_of_block);

  // The file descriptor could be negative if the file failed to open.
  if (current_file_descriptor_ >= 0) {
    // Overcommit, i.e. write everything we consumed before starting a new file for the next
    // block as we cannot have a block spanning multiple files.
    write(current_file_descriptor_, str.c_str(), str.size());
  }

  if (end_of_block) {
    StartNewFile();
  }
}

void SystemLogWriter::RotateFilePaths() {
  TRACE_DURATION("feedback:io", "SystemLogWriter::RotateFilePaths");

  // Assuming we have 4 files file0.txt, file1.txt, file2.txt, and file3.txt, in that order, their
  // names will change as follows:
  // files2.txt -> file3.txt, file1.txt -> file2.txt, file0.txt -> file1.txt.
  // The contents of file3.txt no longer exist.
  for (size_t i = file_paths_.size() - 1; i > 0; --i) {
    rename(file_paths_[i - 1].c_str(), file_paths_[i].c_str());
  }
}

}  // namespace feedback_data
}  // namespace forensics
