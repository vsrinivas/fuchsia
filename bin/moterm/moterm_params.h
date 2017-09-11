// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_MOTERM_PARAMS_H_
#define APPS_MOTERM_MOTERM_PARAMS_H_

#include "lib/fxl/command_line.h"

namespace moterm {

struct MotermParams {
  MotermParams();
  ~MotermParams();

  bool Parse(const fxl::CommandLine& command_line);

  std::vector<std::string> command;
  uint32_t font_size = 12;
};

}  // namespace moterm

#endif  // APPS_MOTERM_MOTERM_PARAMS_H_
