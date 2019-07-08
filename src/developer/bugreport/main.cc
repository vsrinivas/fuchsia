// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>
#include <stdio.h>
#include <stdlib.h>

#include <set>
#include <string>

#include "src/developer/bugreport/bug_reporter.h"
#include "src/developer/bugreport/command_line_options.h"

namespace {

using fuchsia::bugreport::Mode;

}  // namespace

int main(int argc, char** argv) {
  const Mode mode = fuchsia::bugreport::ParseModeFromArgcArgv(argc, argv);
  std::set<std::string> attachment_allowlist;
  switch (mode) {
    case Mode::FAILURE:
      return EXIT_FAILURE;
    case Mode::HELP:
      return EXIT_SUCCESS;
    case Mode::MINIMAL:
      attachment_allowlist = {"inspect.json"};
    case Mode::DEFAULT:
      auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();

      return fuchsia::bugreport::MakeBugReport(environment_services, attachment_allowlist)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
  }
}
