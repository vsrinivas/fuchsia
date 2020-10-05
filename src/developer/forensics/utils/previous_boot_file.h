// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_PREVIOUS_BOOT_FILE_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_PREVIOUS_BOOT_FILE_H_

#include <string>

namespace forensics {

// Manages moving a file from a previous boot (stored in either /data or /cache) to /tmp the first
// time a component is instantiated so it is accessible across component restarts, but not reboots.
class PreviousBootFile {
 public:
  // Move /data/<file> to /tmp/<file>.
  static PreviousBootFile FromData(bool is_first_instance, const std::string& file);

  // Move /cache/<file> to /tmp/<file>.
  static PreviousBootFile FromCache(bool is_first_instance, const std::string& file);

  // The path where data from this boot should be stored.
  const std::string& CurrentBootPath() const { return current_boot_path_; }

  // The path where data from the previous boot is stored. A null value indicates that the data
  // wasn't successfully moved.
  const std::string& PreviousBootPath() const { return previous_boot_path_; }

 private:
  PreviousBootFile(bool is_first_instance, const std::string& to, const std::string& from);

  std::string current_boot_path_;
  std::string previous_boot_path_;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_PREVIOUS_BOOT_FILE_H_
