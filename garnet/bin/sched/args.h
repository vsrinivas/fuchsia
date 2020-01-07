// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SCHED_ARGS_H_
#define GARNET_BIN_SCHED_ARGS_H_

#include <lib/cmdline/args_parser.h>

#include <optional>
#include <string>
#include <vector>

namespace sched {

struct CommandLineArgs {
  // Desired priority.
  int priority = -1;

  // Verbose printing.
  bool verbose = false;

  // Show help.
  bool help = false;

  // Remaining command line arguments.
  std::vector<std::string> params;
};

CommandLineArgs ParseArgsOrExit(int argc, const char** argv);

}  // namespace sched

#endif  // GARNET_BIN_SCHED_ARGS_H_
