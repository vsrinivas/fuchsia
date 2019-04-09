// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tests/run_test.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/spawn.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <string>
#include <vector>

#include "garnet/bin/trace/spec.h"
#include "src/lib/files/file.h"

// The "path" of the trace program from outside the trace package.
const char kTraceProgramUrl[] =
    "fuchsia-pkg://fuchsia.com/trace#meta/trace.cmx";
// The path of the trace program from within the trace package.
// const char kTracePackageProgramPath[] = "/pkg/bin/trace";

// Package path to use for spawned processes.
const char kSystemPackageTestPrefix[] = "/pkgfs/packages/trace_tests/0/";
// Package path to use for launched processes.
const char kPackageTestPrefix[] = "/pkg/";

void AppendLoggingArgs(std::vector<std::string>* argv, const char* prefix) {
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
      verbose_or_quiet_arg =
          fxl::StringPrintf("%s--quiet=%d", prefix, log_settings.min_log_level);
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

static bool ReadTspec(const std::string& tspec_path, tracing::Spec* spec) {
  std::string tspec_contents;
  if (!files::ReadFileToString(tspec_path, &tspec_contents)) {
    FXL_LOG(ERROR) << "Can't read test spec: " << tspec_path;
    return false;
  }

  if (!tracing::DecodeSpec(tspec_contents, spec)) {
    FXL_LOG(ERROR) << "Error decoding test spec: " << tspec_path;
    return false;
  }
  return true;
}

static void BuildTraceProgramArgv(const std::string& relative_tspec_path,
                                  const std::string& output_file_path,
                                  std::vector<std::string>* argv) {
  tracing::Spec spec;
  if (!ReadTspec(kPackageTestPrefix + relative_tspec_path, &spec))
    return;

  argv->push_back(kTraceProgramUrl);
  AppendLoggingArgs(argv, "");
  argv->push_back("record");
  argv->push_back(fxl::StringPrintf(
      "--spec-file=%s",
      (kSystemPackageTestPrefix + relative_tspec_path).c_str()));
  argv->push_back(
      fxl::StringPrintf("--output-file=%s", output_file_path.c_str()));

  AppendLoggingArgs(argv, "--append-args=");

  // Note that |relative_tspec_path| cannot have a comma.
  argv->push_back(fxl::StringPrintf(
      "--append-args=run,%s",
      ((spec.spawn ? kSystemPackageTestPrefix : kPackageTestPrefix) +
       relative_tspec_path)
          .c_str()));
}

static void BuildVerificationProgramArgv(const std::string& program_path,
                                         const std::string& relative_tspec_path,
                                         const std::string& output_file_path,
                                         std::vector<std::string>* argv) {
  argv->push_back(program_path);

  AppendLoggingArgs(argv, "");

  argv->push_back("verify");
  argv->push_back(relative_tspec_path);
  argv->push_back(output_file_path);
}

zx_status_t SpawnProgram(const zx::job& job,
                         const std::vector<std::string>& argv,
                         zx_handle_t arg_handle, zx::process* out_process) {
  std::vector<const char*> c_argv;
  StringArgvToCArgv(argv, &c_argv);

  FXL_VLOG(1) << "Running " << fxl::JoinStrings(argv, " ");

  size_t action_count = 0;
  fdio_spawn_action_t spawn_actions[1];
  if (arg_handle != ZX_HANDLE_INVALID) {
    spawn_actions[0].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    spawn_actions[0].h.id = PA_HND(PA_USER0, 0);
    spawn_actions[0].h.handle = arg_handle;
    action_count = 1;
  }

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  auto status =
      fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, c_argv[0], c_argv.data(),
                     nullptr, action_count, &spawn_actions[0],
                     out_process->reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Spawning " << c_argv[0] << " failed: " << err_msg << ", "
                   << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t WaitAndGetExitCode(const std::string& program_name,
                               const zx::process& process, int* out_exit_code) {
  auto status =
      process.wait_one(ZX_PROCESS_TERMINATED,
                       zx::deadline_after(zx::duration(kTestTimeout)), nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed waiting for program " << program_name
                   << " to exit: " << zx_status_get_string(status);
    return status;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(process.get(), ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error getting return code for program " << program_name
                   << ": " << zx_status_get_string(status);
    return status;
  }

  if (proc_info.return_code != 0) {
    FXL_LOG(ERROR) << program_name << " exited with exit code "
                   << proc_info.return_code;
  }
  *out_exit_code = proc_info.return_code;
  return ZX_OK;
}

static bool LaunchTool(const std::vector<std::string>& argv) {
  zx::job job{};  // -> default job
  zx::process subprocess;

  auto status = SpawnProgram(job, argv, ZX_HANDLE_INVALID, &subprocess);
  if (status != ZX_OK) {
    return false;
  }

  int exit_code;
  status = WaitAndGetExitCode(argv[0], subprocess, &exit_code);
  if (status != ZX_OK) {
    return false;
  }
  if (exit_code != 0) {
    return false;
  }

  return true;
}

static bool LaunchApp(sys::ComponentContext* context, const std::string& app,
                      const std::vector<std::string>& args) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = std::string(app);
  launch_info.arguments = fidl::To<fidl::VectorPtr<std::string>>(args);

  if (FXL_VLOG_IS_ON(1)) {
    FXL_VLOG(1) << "Launching: " << launch_info.url << " "
                << fxl::JoinStrings(args, " ");
  } else {
    FXL_LOG(INFO) << "Launching: " << launch_info.url;
  }

  fuchsia::sys::ComponentControllerPtr component_controller;
  int64_t return_code = INT64_MIN;

  // Attach to the current thread so that it's using the default async
  // dispatcher, which is what the component controller machinery is using.
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fuchsia::sys::LauncherPtr launcher;
  context->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info),
                            component_controller.NewRequest());
  component_controller.set_error_handler(
      [&loop, &return_code](zx_status_t error) {
        // This can get run even after the app has exited, in the destructor.
        if (return_code == INT64_MIN) {
          FXL_LOG(INFO) << "Application terminated, status " << error;
          return_code = error;
          loop.Quit();
        }
      });
  component_controller.events().OnTerminated =
      [&loop, &return_code](
          int64_t rc, fuchsia::sys::TerminationReason termination_reason) {
        FXL_LOG(INFO) << "Application exited with return reason/code "
                      << static_cast<int>(termination_reason) << "/" << rc;
        switch (termination_reason) {
          case fuchsia::sys::TerminationReason::UNKNOWN:
            // Some non-zero value.
            return_code = 1;
            break;
          case fuchsia::sys::TerminationReason::EXITED:
            return_code = rc;
            break;
          default:
            return_code = static_cast<int64_t>(termination_reason);
            break;
        }
        loop.Quit();
      };

  // We could add a timeout here but the general rule is to leave it to the
  // watchdog timer.

  loop.Run();

  FXL_LOG(INFO) << "return_code " << return_code;
  return return_code == 0;
}

bool RunTspec(sys::ComponentContext* context,
              const std::string& relative_tspec_path,
              const std::string& output_file_path) {
  std::vector<std::string> argv;
  BuildTraceProgramArgv(relative_tspec_path, output_file_path, &argv);

  FXL_LOG(INFO) << "Running tspec " << relative_tspec_path << ", output file "
                << output_file_path;

  return LaunchApp(context, argv[0],
                   std::vector<std::string>(argv.begin() + 1, argv.end()));
}

bool VerifyTspec(sys::ComponentContext* context,
                 const std::string& relative_tspec_path,
                 const std::string& output_file_path) {
  tracing::Spec spec;
  if (!ReadTspec(kPackageTestPrefix + relative_tspec_path, &spec)) {
    return false;
  }

  FXL_DCHECK(spec.app);
  const std::string& program_path = *spec.app;

  std::vector<std::string> argv;
  BuildVerificationProgramArgv(
      program_path,
      ((spec.spawn ? kSystemPackageTestPrefix : kPackageTestPrefix) +
       relative_tspec_path),
      output_file_path, &argv);

  FXL_LOG(INFO) << "Verifying tspec " << relative_tspec_path << ", output file "
                << output_file_path;

  // For consistency we do the exact same thing that the trace program does.
  // We also use the same function names for easier comparison.
  if (spec.spawn) {
    return LaunchTool(argv);
  } else {
    return LaunchApp(context, argv[0],
                     std::vector<std::string>(argv.begin() + 1, argv.end()));
  }
}
