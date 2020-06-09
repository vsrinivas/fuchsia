// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/system_log_recorder/writer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace feedback {

SystemLogWriter::SystemLogWriter(const std::vector<const std::string>& file_paths,
                                 const FileSize total_log_size, LogMessageStore* store)
    : file_paths_(file_paths),
      individual_file_size_(total_log_size / file_paths.size()),
      current_file_(individual_file_size_),
      store_(store) {
  FX_CHECK(file_paths_.size() > 0);
  StartNewFile();
}

void SystemLogWriter::StartNewFile() { current_file_.Open(file_paths_.front()); }

void SystemLogWriter::Write() {
  TRACE_DURATION("feedback:io", "SystemLogWriter::Write");
  const std::string str = store_->Consume();

  FX_CHECK(individual_file_size_.to_bytes() > str.size());

  if (current_file_.BytesRemaining() < str.size()) {
    current_file_.Close();
    RotateFilePaths();
    StartNewFile();
  }

  current_file_.Write(str);
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

}  // namespace feedback
