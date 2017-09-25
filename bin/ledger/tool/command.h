// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TOOL_COMMAND_H_
#define PERIDOT_BIN_LEDGER_TOOL_COMMAND_H_

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

namespace tool {

class Command {
 public:
  Command() {}
  virtual ~Command() {}

  virtual void Start(fxl::Closure on_done) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Command);
};

}  // namespace tool

#endif  // PERIDOT_BIN_LEDGER_TOOL_COMMAND_H_
