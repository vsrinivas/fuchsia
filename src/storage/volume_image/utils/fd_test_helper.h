// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_TEST_HELPER_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_TEST_HELPER_H_

#include <lib/fpromise/result.h>

#include <string>
#include <string_view>

namespace storage::volume_image {

// RAII for temporary files.
class TempFile {
 public:
  // On Success return a |TempFile| in the system's temporary directory.
  //
  // On error returns a string describing the failure reason.
  static fpromise::result<TempFile, std::string> Create();

  TempFile() = default;
  TempFile(const TempFile&) = delete;
  TempFile(TempFile&&) = default;
  TempFile& operator=(const TempFile&) = delete;
  TempFile& operator=(TempFile&&) = default;
  ~TempFile();

  // Returns the path to the newly created file.
  std::string_view path() const { return path_; }

 private:
  explicit TempFile(std::string_view path) : path_(path) {}
  std::string path_;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_FD_TEST_HELPER_H_
