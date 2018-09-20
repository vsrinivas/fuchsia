// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <lib/fdio/spawn.h>
#include <lib/fxl/log_settings.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/join_strings.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/types.h>
#include <zircon/status.h>

#include "garnet/bin/trace/tests/run_test.h"

// The path to the test subprogram.
// This path can only be interpreted within the context of the test package.
#define TEST_APP_PATH "/pkg/bin/integration_test_app"

// The path of the trace program.
const char kTraceProgramPath[] = "/system/bin/trace";

// Where trace output is written.
// TODO(dje): This is the current default, /data is going away.
#define TRACE_OUTPUT_FILE "/data/trace.json"

// For now don't run longer than this. The CQ bot has this timeout as well,
// so this is as good a value as any. Later we might want to add a timeout
// value to tspecs.
constexpr zx_duration_t kTestTimeout = ZX_SEC(60);

static void AppendLoggingArgs(std::vector<std::string>* argv,
                              const char* prefix) {
  // Transfer our log settings to the subprogram.
  auto log_settings = fxl::GetLogSettings();
  std::string log_file_arg;
  std::string verbose_or_quiet_arg;
  if (log_settings.log_file != "") {
    log_file_arg = fxl::StringPrintf("%s--log-file=%s", prefix,
                                     log_settings.log_file.c_str());
    argv->push_back(log_file_arg);
  }
  if (log_settings.min_log_level != 0) {
    if (log_settings.min_log_level < 0) {
      verbose_or_quiet_arg = fxl::StringPrintf("%s--verbose=%d", prefix,
                                               -log_settings.min_log_level);
    } else {
      verbose_or_quiet_arg = fxl::StringPrintf("%s--quiet=%d", prefix,
                                               log_settings.min_log_level);
    }
    argv->push_back(verbose_or_quiet_arg);
  }
}

static void StringArgvToCArgv(std::vector<std::string>* argv,
                              std::vector<const char*>* c_argv) {
  for (const auto& arg : *argv) {
    c_argv->push_back(arg.c_str());
  }
  c_argv->push_back(nullptr);
}

static void BuildTraceProgramArgv(const std::string& tspec_path,
                                  const std::string& output_file_path,
                                  std::vector<std::string>* argv) {
  argv->push_back(kTraceProgramPath);
  AppendLoggingArgs(argv, "");
  argv->push_back("record");
  argv->push_back(fxl::StringPrintf("--spec-file=%s", tspec_path.c_str()));
  argv->push_back(fxl::StringPrintf("--output-file=%s",
                                    output_file_path.c_str()));

  AppendLoggingArgs(argv, "--append-args=");

  // Note that |tspec_path| cannot have a comma.
  argv->push_back(fxl::StringPrintf("--append-args=run,%s",
                                    tspec_path.c_str()));
}

static void BuildVerificationProgramArgv(const std::string& tspec_path,
                                         const std::string& output_file_path,
                                         std::vector<std::string>* argv) {
  argv->push_back(TEST_APP_PATH);

  AppendLoggingArgs(argv, "");

  argv->push_back("verify");
  argv->push_back(tspec_path.c_str());
  argv->push_back(output_file_path.c_str());
}

// |verify=false| -> run the test
// |verify=true| -> verify the test
static bool RunTspecWorker(const std::string& tspec_path,
                           const std::string& output_file_path,
                           bool verify) {
  const char* operation_name = verify ? "Verifying" : "Running";
  FXL_LOG(INFO) << operation_name << " tspec " << tspec_path;
  zx_handle_t job = ZX_HANDLE_INVALID; // -> default job
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx::process subprocess;

  std::vector<std::string> argv;
  if (!verify) {
    BuildTraceProgramArgv(tspec_path, output_file_path, &argv);
  } else {
    BuildVerificationProgramArgv(tspec_path, output_file_path, &argv);
  }

  std::vector<const char*> c_argv;
  StringArgvToCArgv(&argv, &c_argv);

  FXL_VLOG(1) << "Running " << fxl::JoinStrings(argv, " ");

  auto status = fdio_spawn_etc(job, FDIO_SPAWN_CLONE_ALL,
                               c_argv[0], c_argv.data(), nullptr,
                               0, nullptr, subprocess.reset_and_get_address(),
                               err_msg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Spawning " << c_argv[0] << " failed: " << err_msg;
    return false;
  }

  status = subprocess.wait_one(ZX_PROCESS_TERMINATED,
                               zx::deadline_after(zx::duration(kTestTimeout)),
                               nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed waiting for subprogram to exit: "
                   << zx_status_get_string(status);
    return false;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(subprocess.get(), ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), nullptr, nullptr);
  if (status < 0) {
    FXL_LOG(ERROR) << "Error getting return code: "
                   << zx_status_get_string(status);
    return false;
  }
  if (proc_info.return_code != 0) {
    FXL_LOG(ERROR) << operation_name << " exited with return code "
                   << proc_info.return_code;
    return false;
  }

  FXL_VLOG(1) << operation_name << " completed OK";

  return true;
}

bool RunTspec(const std::string& tspec_path,
              const std::string& output_file_path) {
  return RunTspecWorker(tspec_path, output_file_path, false /*run*/);
}

bool VerifyTspec(const std::string& tspec_path,
                 const std::string& output_file_path) {
  return RunTspecWorker(tspec_path, output_file_path, true /*verify*/);
}
