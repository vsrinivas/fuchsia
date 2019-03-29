// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This command exists to support integrating Zedmon power readings into traceutil.
// The problem to be solved is mapping Zedmon to Fuchsia time domains so that trace data
// from Zedmon can be merged with trace data from the Fuchsia device.
// Data is captured on the devhost, so what we need to do is map devhost times to Fuchsia
// times. This command provides an interactive tool to obtain this mapping.

#include <iostream>
#include <stdint.h>
#include <zircon/syscalls.h>

#include "garnet/bin/trace/commands/time.h"

#include "src/lib/fxl/logging.h"

namespace tracing {

Command::Info Time::Describe() {
  return Command::Info{[](component::StartupContext* context) {
                         return std::make_unique<Time>(context);
                       },
                       "time",
                       "interactively print timestamps",
                       {}};
}

Time::Time(component::StartupContext* context) : Command(context) {}

void Time::Start(const fxl::CommandLine& command_line) {
  if (!(command_line.options().empty() &&
        command_line.positional_args().empty())) {
    FXL_LOG(ERROR) << "We encountered unknown options, please check your "
                   << "command invocation";
    Done(1);
    return;
  }

  out() << "Time sync tool: Input \"t\" to get a tracing timestamp in μs. Input \"q\" to quit."
        << std::endl;

  double tick_scale = 1'000'000.0 / static_cast<double>(zx_ticks_per_second());
  char c;
  while (true) {
    in().get(c);
    switch (c) {
      case 'q':
        return;
      case 't': {
        double timestamp = static_cast<double>(zx_ticks_get()) / tick_scale;
        out() << timestamp << std::endl;
        break;
      }
      default:
        break;
    }
  }
}

}  // namespace tracing
