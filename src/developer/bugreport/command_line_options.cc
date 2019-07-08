// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/bugreport/command_line_options.h"

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/substitute.h"

namespace fuchsia {
namespace bugreport {
namespace {

constexpr char kUsage[] = R"($0

    Dumps in stdout a JSON file containing the feedback data (annotations and
    attachments) collected from fuchsia.feedback.DataProvider.

Usage:

  $0 [--minimal]

Arguments:

    --minimal    Restricts the attachments to the Inspect data only (no logs,
                 no build snapshot, etc.). Annotations are preserved.

)";

}  // namespace

Mode ParseModeFromArgcArgv(int argc, const char* const* argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  auto& pos_args = command_line.positional_args();
  if (command_line.HasOption("help") || (pos_args.size() == 1u && pos_args[0] == "help")) {
    printf("%s\n", fxl::Substitute(kUsage, command_line.argv0()).c_str());
    return Mode::HELP;
  }

  if (command_line.HasOption("minimal")) {
    return Mode::MINIMAL;
  } else if (!command_line.options().empty() || !pos_args.empty()) {
    fprintf(stderr, "Unexpected option. Usage: %s\n",
            fxl::Substitute(kUsage, command_line.argv0()).c_str());
    return Mode::FAILURE;
  }

  return Mode::DEFAULT;
}

}  // namespace bugreport
}  // namespace fuchsia
