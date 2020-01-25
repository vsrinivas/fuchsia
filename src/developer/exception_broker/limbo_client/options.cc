// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/exception_broker/limbo_client/options.h"

#include <zircon/exception.h>
#include <zircon/status.h>

#include "src/developer/exception_broker/limbo_client/limbo_client.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace fuchsia {
namespace exception {

namespace {

zx_status_t EnableLimbo(LimboClient*, const std::vector<const char*>&, std::ostream&);
zx_status_t DisableLimbo(LimboClient*, const std::vector<const char*>&, std::ostream&);
zx_status_t ListLimbo(LimboClient*, const std::vector<const char*>&, std::ostream&);
zx_status_t ReleaseFromLimbo(LimboClient*, const std::vector<const char*>&, std::ostream&);

struct Option {
  std::string name;
  std::string description;
  OptionFunction func = nullptr;
};

const Option kOptions[] = {
    {"enable", "Enable the process limbo. It will now begin to capture crashing processes.",
     EnableLimbo},
    {"disable", "Disable the process limbo. Will free any pending processes waiting in it.",
     DisableLimbo},
    {"list", "Lists the processes currently waiting on limbo. The limbo must be active.",
     ListLimbo},
    {"release",
     "Release a process from limbo. The limbo must be active. Usage: limbo release <pid>.",
     ReleaseFromLimbo},
};

void PrintUsage(std::ostream& os) {
  os << R"(Usage: limbo [--help] <option>

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

zx_status_t EnableLimbo(LimboClient* limbo, const std::vector<const char*>& argv,
                        std::ostream& os) {
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

zx_status_t DisableLimbo(LimboClient* limbo, const std::vector<const char*>& argv,
                         std::ostream& os) {
  if (!limbo->active()) {
    os << "Limbo is already deactivated." << std::endl;
    return ZX_OK;
  }

  if (zx_status_t status = limbo->SetActive(false); status != ZX_OK) {
    os << "Could not deactivate limbo: " << zx_status_get_string(status) << std::endl;
    return status;
  }

  os << "Deactivated the process limbo. All contained processes have been freed." << std::endl;
  return ZX_OK;
}

zx_status_t ListLimbo(LimboClient* client, const std::vector<const char*>& argv, std::ostream& os) {
  if (!client->active()) {
    os << "Process limbo is not active." << std::endl;
    return ZX_OK;
  }

  std::vector<LimboClient::ProcessDescription> processes;
  if (zx_status_t status = client->ListProcesses(&processes); status != ZX_OK) {
    os << "Could not list the process limbo: " << zx_status_get_string(status) << std::endl;
    return status;
  }

  if (processes.empty()) {
    os << "No processes currently on limbo." << std::endl;
    return ZX_OK;
  }

  os << "Processes currently on limbo: " << std::endl;
  for (const auto& process : processes) {
    auto msg = fxl::StringPrintf("%s (pid: %lu), thread %s (tid: %lu) on exception: %s",
                                 process.process_name.c_str(), process.process_koid,
                                 process.thread_name.c_str(), process.thread_koid,
                                 zx_exception_get_string(process.exception));
    os << "- " << msg << std::endl;
  }

  return ZX_OK;
}

zx_status_t ReleaseFromLimbo(LimboClient* client, const std::vector<const char*>& argv,
                             std::ostream& os) {
  if (!client->active()) {
    os << "Process limbo is not active." << std::endl;
    return ZX_OK;
  }

  if (argv.size() != 3u) {
    os << "Release Usage: limbo release <pid>" << std::endl;
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t pid = std::atoll(argv[2]);
  if (pid == 0) {
    os << "Invalid pid " << argv[2] << std::endl;
    os << "Release Usage: limbo release <pid>" << std::endl;
    return ZX_ERR_INVALID_ARGS;
  }

  if (zx_status_t status = client->Release(pid); status != ZX_OK) {
    if (status == ZX_ERR_NOT_FOUND) {
      os << "Could not find pid: " << pid << std::endl;
    }

    return status;
  }

  os << "Successfully release process " << pid << " from limbo." << std::endl;
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
