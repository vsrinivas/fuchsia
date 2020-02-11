// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/file_line.h"

#include <tuple>

namespace zxdb {

FileLine::FileLine() = default;
FileLine::FileLine(std::string file, int line) : file_(std::move(file)), line_(line) {}
FileLine::FileLine(std::string file, std::string comp_dir, int line)
    : file_(std::move(file)), comp_dir_(std::move(comp_dir)), line_(line) {}
FileLine::~FileLine() = default;

bool operator<(const FileLine& a, const FileLine& b) {
  return std::make_tuple(a.line(), a.file(), a.comp_dir()) <
         std::make_tuple(b.line(), b.file(), b.comp_dir());
}

bool operator==(const FileLine& a, const FileLine& b) {
  return a.line() == b.line() && a.file() == b.file() && a.comp_dir() == b.comp_dir();
}

bool operator!=(const FileLine& a, const FileLine& b) { return !operator==(a, b); }

}  // namespace zxdb
