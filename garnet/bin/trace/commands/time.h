// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_COMMANDS_TIME_H_
#define GARNET_BIN_TRACE_COMMANDS_TIME_H_

#include "garnet/bin/trace/command.h"

namespace tracing {

class Time : public Command {
 public:
  static Info Describe();

  explicit Time(sys::ComponentContext* context);
  void Start(const fxl::CommandLine& command_line) override;
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_COMMANDS_TIME_H_
