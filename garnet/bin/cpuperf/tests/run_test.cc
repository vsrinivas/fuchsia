// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <lib/fdio/spawn.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/processargs.h>
#include <zircon/types.h>
#include <zircon/status.h>

#include "run_test.h"

// The path of the trace program.
// cpuperf is a "shell=true" program, and thus has a stub for it in /bin
// that resolves to the component path.
const char kCpuperfProgramPath[] = "/bin/cpuperf";

static void AppendLoggingArgs(std::vector<std::string>* argv) {
  // Transfer our log settings to the subprogram.
  auto log_settings = fxl::GetLogSettings();
  std::string log_file_arg;
  std::string verbose_or_quiet_arg;
  if (log_settings.log_file != "") {
    log_file_arg = fxl::StringPrintf("--log-file=%s",
                                     log_settings.log_file.c_str());
    argv->push_back(log_file_arg);
  }
  if (log_settings.min_log_level != 0) {
    if (log_settings.min_log_level < 0) {
      verbose_or_quiet_arg = fxl::StringPrintf("--verbose=%d",
                                               -log_settings.min_log_level);
    } else {
      verbose_or_quiet_arg = fxl::StringPrintf("--quiet=%d",
                                               log_settings.min_log_level);
    }
    argv->push_back(verbose_or_quiet_arg);
  }
}

static void StringArgvToCArgv(const std::vector<std::string>& argv,
                              std::vector<const char*>* c_argv) {
  for (const auto& arg : argv) {
    c_argv->push_back(arg.c_str());
  }
  c_argv->push_back(nullptr);
}

static void BuildCpuperfProgramArgv(const std::string& spec_path,
                                    std::vector<std::string>* argv) {
  argv->push_back(kCpuperfProgramPath);
  AppendLoggingArgs(argv);
  argv->push_back(fxl::StringPrintf("--spec-file=%s", spec_path.c_str()));
}

static zx_status_t SpawnProgram(const zx::job& job,
                                const std::vector<std::string>& argv,
                                zx::process* out_process) {
  std::vector<const char*> c_argv;
  StringArgvToCArgv(argv, &c_argv);

  FXL_VLOG(1) << "Running " << fxl::JoinStrings(argv, " ");

  size_t action_count = 0;
  fdio_spawn_action_t* spawn_actions = nullptr;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  auto status = fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL,
                               c_argv[0], c_argv.data(), nullptr,
                               action_count, spawn_actions,
                               out_process->reset_and_get_address(),
                               err_msg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Spawning " << c_argv[0] << " failed: "
                   << status << ", " << err_msg;
    return status;
  }

  return ZX_OK;
}

static zx_status_t WaitAndGetExitCode(const std::string& program_name,
                                      const zx::process& process,
                                      int* out_exit_code) {
  auto status = process.wait_one(
    ZX_PROCESS_TERMINATED, zx::deadline_after(zx::duration(kTestTimeout)),
    nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed waiting for program " << program_name
                   << " to exit: " << zx_status_get_string(status);
    return status;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(process.get(), ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error getting return code for program "
                   << program_name << ": " << zx_status_get_string(status);
    return status;
  }

  if (proc_info.return_code != 0) {
    FXL_LOG(ERROR) << program_name << " exited with exit code "
                   << proc_info.return_code;
  }
  *out_exit_code = proc_info.return_code;
  return ZX_OK;
}

bool RunSpec(const std::string& spec_path) {
  FXL_LOG(INFO) << "Running spec " << spec_path;
  zx::job job{}; // -> default job
  zx::process subprocess;

  std::vector<std::string> argv;
  BuildCpuperfProgramArgv(spec_path, &argv);

  auto status = SpawnProgram(job, argv, &subprocess);
  if (status != ZX_OK) {
    return false;
  }

  int exit_code;
  status = WaitAndGetExitCode(argv[0], subprocess, &exit_code);
  if (status != ZX_OK) {
    return false;
  }
  if (exit_code != 0) {
    FXL_LOG(ERROR) << "Running spec terminated: exit code " << exit_code;
    return false;
  }

  FXL_VLOG(1) << "Running spec completed OK";
  return true;
}
