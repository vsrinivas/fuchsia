// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/file_line.h"

#include "garnet/bin/zxdb/common/file_util.h"

namespace zxdb {

FileLine::FileLine() = default;
FileLine::FileLine(std::string file, int line)
    : file_(std::move(file)), line_(line) {}
FileLine::~FileLine() = default;

std::string FileLine::GetFileNamePart() const {
  return ExtractLastFileComponent(file_).ToString();
}

bool operator<(const FileLine& a, const FileLine& b) {
  if (a.line() != b.line())
    return a.line() < b.line();
  return a.file() < b.file();
}

}  // namespace zxdb
