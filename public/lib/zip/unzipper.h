// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZIP_UNZIPPER_H_
#define LIB_ZIP_UNZIPPER_H_

#include <string>
#include <vector>

#include "lib/zip/unique_unzipper.h"
#include "lib/fxl/macros.h"

namespace zip {

class Unzipper {
 public:
  explicit Unzipper(std::vector<char> buffer);
  ~Unzipper();

  std::vector<char> Extract(const std::string& path);

  const std::vector<char>& buffer() const { return buffer_; }

 private:
  std::vector<char> buffer_;
  UniqueUnzipper decoder_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Unzipper);
};

}  // namespace zip

#endif  // LIB_ZIP_UNZIPPER_H_
