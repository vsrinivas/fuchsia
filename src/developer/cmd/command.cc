// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/command.h"

namespace cmd {

static const char kWhitespace[] = " \t\r\n";

Command::Command() = default;

Command::~Command() = default;

void Command::Parse(const std::string& line) {
  args_.clear();
  size_t pos = 0;
  while (pos != std::string::npos) {
    size_t start = line.find_first_not_of(kWhitespace, pos);
    if (start == std::string::npos) {
      return;
    }
    if (line[start] == '#') {
      return;
    }
    size_t end = line.find_first_of(kWhitespace, start);
    args_.push_back(line.substr(start, end - start));
    pos = end;
  }
}

}  // namespace cmd
