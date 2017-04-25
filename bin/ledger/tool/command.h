// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TOOL_COMMAND_H_
#define APPS_LEDGER_SRC_TOOL_COMMAND_H_

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace tool {

class Command {
 public:
  Command() {}
  virtual ~Command() {}

  virtual void Start(ftl::Closure on_done) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Command);
};

}  // namespace tool

#endif  // APPS_LEDGER_SRC_TOOL_COMMAND_H_
