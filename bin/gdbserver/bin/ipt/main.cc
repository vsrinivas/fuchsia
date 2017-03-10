// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): wip wip wip

#include <cinttypes>
#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>

#include <mxio/util.h>

#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/ftl/strings/string_number_conversions.h"

#ifdef __x86_64__  // for other arches we're just a stub, TO-128

#include <magenta/device/intel-pt.h>
#include <magenta/device/ktrace.h>
#include <magenta/ktrace.h>
#include <magenta/syscalls.h>

#include "debugger-utils/util.h"
#include "debugger-utils/x86-pt.h"

#include "inferior-control/arch.h"
#include "inferior-control/arch-x86.h"

#include "control.h"
#include "server.h"

constexpr char kUsageString[] =
    "Usage: ipt [options] program [args...]\n"
    "       ipt [options] --control action1 [action2 ...]\n"
    "\n"
    "  program - the path to the executable to run\n"
    "\n"
    "Actions (performed when --control is specified):\n"
    "These cannot be specified with a program to run.\n"
    "  init               allocate PT resources (buffers)\n"
    "  start              turn on PT\n"
    "  stop               turn off PT\n"
    "  dump               dump PT data\n"
    "  reset              reset PT (release all resources)\n"
    "\n"
    "Options:\n"
    "  --control          perform the specified actions\n"
    "  --dump-arch        print random facts about the architecture and exit\n"
    "  --help             show this help message and exit\n"
    "  --quiet[=level]    set quietness level (opposite of verbose)\n"
    "  --verbose[=level]  set debug verbosity level\n"
    "\n"
    "IPT configuration options:\n"
    "  --buffer-order=N   set buffer size, in pages, as a power of 2\n"
    "                     The default is 2: 16KB buffers.\n"
    "  --circular         use a circular trace buffer\n"
    "                     Otherwise tracing stops when the buffer fills.\n"
    "  --ctl-config=BITS  set user-settable bits in CTL MSR\n"
    "                     See Intel docs on IA32_RTIT_CTL MSR.\n"
    "  --mode=cpu|thread  set the tracing mode\n"
    "                     Must be specified with a program to run.\n"
    "  --num-buffers=N    set number of buffers\n"
    "                     The default is 16.\n"
    "\n"
    "Notes:\n"
    "--verbose=<level> : sets |min_log_level| to -level\n"
    "--quiet=<level>   : sets |min_log_level| to +level\n"
    "Quiet supersedes verbose if both are specified.\n"
    "Defined log levels:\n"
    "-n - verbosity level n\n"
    " 0 - INFO - this is the default level\n"
    " 1 - WARNING\n"
    " 2 - ERROR\n"
    " 3 - FATAL\n"
    "Note that negative log levels mean more verbosity.\n";

static void PrintUsageString() {
  std::cout << kUsageString << std::endl;
}

static debugserver::IptConfig GetIptConfig(const ftl::CommandLine& cl) {
  debugserver::IptConfig config;
  std::string arg;

  if (cl.GetOptionValue("buffer-order", &arg)) {
    size_t buffer_order;
    if (!ftl::StringToNumberWithError<size_t>(ftl::StringView(arg),
                                              &buffer_order)) {
      FTL_LOG(ERROR) << "Not a valid buffer order: " << arg;
      exit(EXIT_FAILURE);
    }
    config.buffer_order = buffer_order;
  }

  if (cl.HasOption("circular", nullptr)) {
    config.is_circular = true;
  }

  if (cl.GetOptionValue("ctl-config", &arg)) {
    uint64_t ctl_config;
    if (!ftl::StringToNumberWithError<uint64_t>(ftl::StringView(arg),
                                                &ctl_config, ftl::Base::k16)) {
      FTL_LOG(ERROR) << "Not a valid CTL config value: " << arg;
      exit(EXIT_FAILURE);
    }
    config.ctl_config = ctl_config;
  }

  if (cl.GetOptionValue("mode", &arg)) {
    uint32_t mode;
    if (arg == "cpu") {
      mode = IPT_MODE_CPUS;
    } else if (arg == "thread") {
      mode = IPT_MODE_THREADS;
    } else {
      FTL_LOG(ERROR) << "Not a valid mode value: " << arg;
      exit(EXIT_FAILURE);
    }
    config.mode = mode;
  }

  if (cl.GetOptionValue("num-buffers", &arg)) {
    size_t num_buffers;
    if (!ftl::StringToNumberWithError<size_t>(ftl::StringView(arg),
                                              &num_buffers)) {
      FTL_LOG(ERROR) << "Not a valid buffer size: " << arg;
      exit(EXIT_FAILURE);
    }
    config.num_buffers = num_buffers;
  }

  return config;
}

static bool ControlIpt(const debugserver::IptConfig& config, const ftl::CommandLine& cl) {
  // We only support the cpu mode here.
  // This isn't a full test as we only actually set the mode for "init".
  // But it catches obvious mistakes like passing --mode=thread.
  if (config.mode != IPT_MODE_CPUS) {
    FTL_LOG(ERROR) << "--control requires cpu mode";
    return false;
  }

  for (const std::string& action : cl.positional_args()) {
    if (action == "init") {
      if (!SetPerfMode(config))
	return false;
      if (!debugserver::InitCpuPerf(config))
	return false;
      if (!debugserver::InitPerfPreProcess(config))
	return false;
    } else if (action == "start") {
      if (!debugserver::StartCpuPerf(config)) {
	FTL_LOG(WARNING) << "Start failed, but buffers not removed";
	return false;
      }
    } else if (action == "stop") {
      debugserver::StopCpuPerf(config);
      debugserver::StopPerf(config);
    } else if (action == "dump") {
      debugserver::DumpCpuPerf(config);
      debugserver::DumpPerf(config);
    } else if (action == "reset") {
      debugserver::ResetCpuPerf(config);
      debugserver::ResetPerf(config);
    } else {
      FTL_LOG(ERROR) << "Unrecognized action: " << action;
      return false;
    }
  }

  return true;
}

static bool RunProgram(const debugserver::IptConfig& config, const ftl::CommandLine& cl) {
  debugserver::util::Argv inferior_argv(cl.positional_args().begin(),
					cl.positional_args().end());

  if (inferior_argv.size() == 0) {
    FTL_LOG(ERROR) << "Missing program";
    return false;
  }

  debugserver::IptServer ipt(config);

  auto inferior = new debugserver::Process(&ipt, &ipt);
  inferior->set_argv(inferior_argv);

  ipt.set_current_process(inferior);

  return ipt.Run();
}

int main(int argc, char* argv[]) {
  ftl::CommandLine cl = ftl::CommandLineFromArgcArgv(argc, argv);

  if (!ftl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  if (cl.HasOption("dump-arch", nullptr)) {
    debugserver::arch::DumpArch(stdout);
    return EXIT_SUCCESS;
  }

  if (!debugserver::arch::x86::HaveProcessorTrace()) {
    FTL_LOG(ERROR) << "PT not supported";
    return EXIT_FAILURE;
  }

  debugserver::IptConfig config = GetIptConfig(cl);

  FTL_LOG(INFO) << "ipt control program starting";

  bool success;
  if (cl.HasOption("control", nullptr)) {
    success = ControlIpt(config, cl);
  } else {
    success = RunProgram(config, cl);
  }

  if (!success) {
    FTL_LOG(INFO) << "ipt exited with error";
    return EXIT_FAILURE;
  }

  FTL_LOG(INFO) << "ipt control program exiting";
  return EXIT_SUCCESS;
}

#else  // !__x86_64

int main(int argc, char* argv[]) {
  FTL_LOG(ERROR) << "ipt is for x86_64 only";
  return EXIT_FAILURE;
}

#endif  // !__x86_64
