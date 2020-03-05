// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_ROTATING_FILE_SET_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_ROTATING_FILE_SET_H_

#include <deque>
#include <vector>

#include "src/developer/feedback/utils/file_size.h"
#include "src/developer/feedback/utils/write_only_file.h"

namespace feedback {

// Rotating file set allows for recording a fixed amount of text data in a number of files such that
// the most recent data is always present.
//
// Take the example of 3 files with a |total_size| of 8 bytes, 0.txt, 1.txt, and 2.txt, that make up
// the set, in that order. If we wish to write the string 'bytesX' to the set 4 times, the set
// evolves as follows:
//
//  write bytes0:
//    0.txt: bytes0
//    1.txt:
//    2.txt:
//  write bytes1:
//    0.txt: bytes1
//    1.txt: bytes0
//    2.txt:
//  write bytes2:
//    0.txt: bytes2
//    1.txt: bytes1
//    2.txt: bytes0
//  write bytes3:
//    0.txt: bytes3
//    1.txt: bytes2
//    2.txt: bytes1
//
// Additionally, it's important to note that a file will be truncated when it is opend for use by
// the set.
class RotatingFileSetWriter {
 public:
  RotatingFileSetWriter(const std::vector<const std::string>& file_paths, FileSize total_size);

  void Write(const std::string& line);

 private:
  void RotateFilePaths();

  const std::vector<const std::string> file_paths_;
  const FileSize individual_file_size_;

  WriteOnlyFile current_file_;
};

class RotatingFileSetReader {
 public:
  RotatingFileSetReader(const std::vector<const std::string>& file_paths);

  // Returns true if data is written to |file_path|. If no data is written to |file_path|, the file
  // will not be created.
  bool Concatenate(const std::string& file_path) const;

 private:
  const std::vector<const std::string> file_paths_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_ROTATING_FILE_SET_H_
