// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_WRITE_ONLY_FILE_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_WRITE_ONLY_FILE_H_

#include <cstdint>
#include <string>

#include "src/developer/feedback/utils/file_size.h"

namespace feedback {

// Allows writing a predefined number of bytes (|capacity|) to a file.
class WriteOnlyFile {
 public:
  WriteOnlyFile(FileSize capacity);
  ~WriteOnlyFile();

  // Open and truncate the file at |path|.
  //
  // Return true if successful.
  bool Open(const std::string& path);

  // Close the underlying file.
  void Close();

  // Write |str| to the opened file.
  //
  // Return false if |str| cannot be written, either because the file doesn't have enough capacity
  // or the file isn't open.
  bool Write(const std::string& str);

  // Returns the number of bytes of capacity remaining in the file.
  uint64_t BytesRemaining() const;

 private:
  int fd_ = -1;

  const FileSize capacity_;
  FileSize capacity_remaining_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_WRITE_ONLY_FILE_H_
