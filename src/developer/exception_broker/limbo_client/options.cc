// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/exception_broker/limbo_client/options.h"

#include <zircon/status.h>

#include "src/developer/exception_broker/limbo_client/limbo_client.h"

namespace fuchsia {
namespace exception {

namespace {

zx_status_t EnableLimbo(LimboClient*, std::ostream&);

struct Option {
  std::string name;
  std::string description;
  OptionFunction func = nullptr;
};

const Option kOptions[] = {
    {"enable", "Enable the process limbo. It will now begin to capture crashing processes.",
     EnableLimbo},
    /* {"disable", "Disable the process limbo. Will free any pending processes waiting in it.", */
    /*  DisableLimbo}, */
};

void PrintUsage(std::ostream& os) {
  os << R"(Usage: limbo [--help] <option>" << std::endl;

  The process limbo is a service that permits the system to suspend any processes that throws an
  exception (crash) for later processing/debugging. This CLI tool permits to query and modify the
  state of the limbo.

  Options:
    --help: Prints this message.
)";

  for (const Option& option : kOptions) {
    os << "    " << option.name << ": " << option.description << std::endl;
  }
}

// Actions Implementations -------------------------------------------------------------------------

zx_status_t EnableLimbo(LimboClient* limbo, std::ostream& os) {
  if (limbo->active()) {
    os << "Limbo is already active." << std::endl;
    return ZX_OK;
  }

  if (zx_status_t status = limbo->SetActive(true); status != ZX_OK) {
    os << "Could not activate limbo: " << zx_status_get_string(status) << std::endl;
    return status;
  }

  os << "Activated the process limbo." << std::endl;
  return ZX_OK;
}

}  // namespace

OptionFunction ParseArgs(int argc, const char* argv[], std::ostream& os) {
  if (argc == 1 || std::string(argv[1]) == "--help") {
    PrintUsage(os);
    return nullptr;
  }

  for (const Option& option : kOptions) {
    if (option.name == argv[1])
      return option.func;
  }

  os << "Could not find option: " << argv[1] << std::endl;
  PrintUsage(os);
  return nullptr;
}

}  // namespace exception
}  // namespace fuchsia
