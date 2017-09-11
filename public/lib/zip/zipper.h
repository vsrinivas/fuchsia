// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZIP_ZIPPER_H_
#define LIB_ZIP_ZIPPER_H_

#include <string>
#include <vector>

#include "lib/zip/unique_zipper.h"
#include "lib/fxl/macros.h"

namespace zip {

class Zipper {
 public:
  Zipper();
  ~Zipper();

  bool AddCompressedFile(const std::string& path,
                         const char* data,
                         size_t size);

  std::vector<char> Finish();

 private:
  std::vector<char> buffer_;
  UniqueZipper encoder_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Zipper);
};

}  // namespace zip

#endif  // LIB_ZIP_ZIPPER_H_
