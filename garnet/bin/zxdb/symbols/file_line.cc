// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/file_line.h"

#include "src/developer/debug/zxdb/common/file_util.h"

namespace zxdb {

FileLine::FileLine() = default;
FileLine::FileLine(std::string file, int line)
    : file_(std::move(file)), line_(line) {}
FileLine::~FileLine() = default;

std::string FileLine::GetFileNamePart() const {
  return std::string(ExtractLastFileComponent(file_));
}

bool operator<(const FileLine& a, const FileLine& b) {
  if (a.line() != b.line())
    return a.line() < b.line();
  return a.file() < b.file();
}

bool operator==(const FileLine& a, const FileLine& b) {
  return a.line() == b.line() && a.file() == b.file();
}

bool operator!=(const FileLine& a, const FileLine& b) {
  return !operator==(a, b);
}

}  // namespace zxdb
