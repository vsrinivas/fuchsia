// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/moterm/moterm_params.h"

#include <string>

namespace moterm {

MotermParams::MotermParams() {}

MotermParams::~MotermParams() {}

bool MotermParams::Parse(const ftl::CommandLine& command_line) {
  std::string value;
  if (command_line.GetOptionValue("command", &value)) {
    command = value;
  }
  if (command_line.GetOptionValue("font-size", &value)) {
    font_size = std::stoi(value);
  }
  return true;
}

}  // namespace moterm
