// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_COMMANDS_DUMP_PROVIDER_H_
#define GARNET_BIN_TRACE_COMMANDS_DUMP_PROVIDER_H_

#include "garnet/bin/trace/command.h"

namespace tracing {

class DumpProvider : public CommandWithTraceController {
 public:
  static Info Describe();

  explicit DumpProvider(app::ApplicationContext* context);
  void Run(const fxl::CommandLine& command_line) override;
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_COMMANDS_DUMP_PROVIDER_H_
