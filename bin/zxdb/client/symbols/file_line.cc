// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/file_line.h"

namespace zxdb {

FileLine::FileLine() = default;
FileLine::FileLine(const std::string& file, int line)
    : file_(file), line_(line) {}
FileLine::~FileLine() = default;

std::string FileLine::GetFileNamePart() const {
  size_t last_slash = file_.rfind('/');
  if (last_slash == std::string::npos)
    return file_;
  return file_.substr(last_slash + 1);
}

}  // namespace zxdb
