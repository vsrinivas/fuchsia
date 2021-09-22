// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/tracing/lib/test_utils/run_program.h"

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/files/file.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace tracing {
namespace test {

void AppendLoggingArgs(std::vector<std::string>* argv, const char* prefix,
                       const syslog::LogSettings& log_settings) {
  // Transfer our log settings to the subprogram.
  std::string log_file_arg;
  std::string verbose_or_quiet_arg;
  if (log_settings.min_log_level != 0) {
    if (log_settings.min_log_level < 0) {
      verbose_or_quiet_arg =
          fxl::StringPrintf("%s--verbose=%d", prefix, -log_settings.min_log_level);
    } else {
      verbose_or_quiet_arg = fxl::StringPrintf("%s--quiet=%d", prefix, log_settings.min_log_level);
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

zx_status_t SpawnProgram(const zx::job& job, const std::vector<std::string>& argv,
                         zx_handle_t arg_handle, zx::process* out_process) {
  size_t num_actions = 0;
  fdio_spawn_action_t spawn_actions[1];

  if (arg_handle != ZX_HANDLE_INVALID) {
    spawn_actions[num_actions].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    spawn_actions[num_actions].h.id = PA_HND(PA_USER0, 0);
    spawn_actions[num_actions].h.handle = arg_handle;
    ++num_actions;
  }

  return RunProgram(job, argv, num_actions, spawn_actions, out_process);
}

zx_status_t RunProgram(const zx::job& job, const std::vector<std::string>& argv, size_t num_actions,
                       const fdio_spawn_action_t* actions, zx::process* out_process) {
  std::vector<const char*> c_argv;
  StringArgvToCArgv(argv, &c_argv);

  FX_LOGS(INFO) << "Running " << fxl::JoinStrings(argv, " ");

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status =
      fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, c_argv[0], c_argv.data(), nullptr,
                     num_actions, actions, out_process->reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Spawning " << c_argv[0] << " failed: " << err_msg;
    return status;
  }

  return ZX_OK;
}

bool WaitAndGetReturnCode(const std::string& program_name, const zx::process& process,
                          int64_t* out_return_code) {
  // Leave it to the test harness to provide a timeout. If it doesn't that's
  // its bug.
  auto status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed waiting for program " << program_name << " to exit";
    return false;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(process.get(), ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                              nullptr, nullptr);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Error getting return code for program " << program_name;
    return false;
  }

  if (proc_info.return_code != 0) {
    FX_LOGS(INFO) << program_name << " exited with return code " << proc_info.return_code;
  }
  *out_return_code = proc_info.return_code;
  return true;
}

bool RunProgramAndWait(const zx::job& job, const std::vector<std::string>& argv, size_t num_actions,
                       const fdio_spawn_action_t* actions) {
  zx::process subprocess;

  auto status = RunProgram(job, argv, num_actions, actions, &subprocess);
  if (status != ZX_OK) {
    return false;
  }

  int64_t return_code;
  if (!WaitAndGetReturnCode(argv[0], subprocess, &return_code)) {
    return false;
  }
  if (return_code != 0) {
    FX_LOGS(ERROR) << argv[0] << " exited with return code " << return_code;
    return false;
  }

  return true;
}

bool RunComponent(sys::ComponentContext* context, const std::string& app,
                  const std::vector<std::string>& args,
                  std::unique_ptr<fuchsia::sys::FlatNamespace> flat_namespace,
                  fuchsia::sys::ComponentControllerPtr* component_controller) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = std::string(app);
  launch_info.arguments = fidl::To<fidl::VectorPtr<std::string>>(args);
  launch_info.flat_namespace = std::move(flat_namespace);

  FX_LOGS(INFO) << "Launching: " << launch_info.url << " " << fxl::JoinStrings(args, " ");

  fuchsia::sys::LauncherPtr launcher;
  zx_status_t status = context->svc()->Connect(launcher.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "context->svc()->Connect() failed for " << app;
    return false;
  }

  launcher->CreateComponent(std::move(launch_info), component_controller->NewRequest());
  return true;
}

bool WaitAndGetReturnCode(const std::string& program_name, async::Loop* loop,
                          fuchsia::sys::ComponentControllerPtr* component_controller,
                          int64_t* out_return_code) {
  fuchsia::sys::TerminationReason termination_reason =
      fuchsia::sys::TerminationReason::INTERNAL_ERROR;
  // This value is not valid unless |termination_reason==EXITED|.
  int64_t return_code = INT64_MIN;

  component_controller->set_error_handler(
      [&loop, &program_name, &termination_reason](zx_status_t error) {
        FX_PLOGS(ERROR, error) << "Unexpected error waiting for " << program_name << " to exit";
        termination_reason = fuchsia::sys::TerminationReason::UNKNOWN;
        loop->Quit();
      });
  component_controller->events().OnTerminated =
      [&loop, &program_name, &component_controller, &termination_reason, &return_code](
          int64_t rc, fuchsia::sys::TerminationReason reason) {
        FX_LOGS(INFO) << "Component " << program_name << " exited with return reason/code "
                      << static_cast<int>(termination_reason) << "/" << rc;
        // Disable the error handler. It can get called after we're done, e.g., during the
        // destructor.
        component_controller->set_error_handler([](zx_status_t error) {});
        termination_reason = reason;
        if (termination_reason == fuchsia::sys::TerminationReason::EXITED) {
          return_code = rc;
        }
        loop->Quit();
      };

  // We could add a timeout here but the general rule is to leave it to the watchdog timer.
  loop->Run();

  if (termination_reason == fuchsia::sys::TerminationReason::EXITED) {
    FX_LOGS(INFO) << program_name << ": return code " << return_code;
    *out_return_code = return_code;
    return true;
  } else {
    FX_LOGS(ERROR) << program_name << ": termination reason "
                   << static_cast<int>(termination_reason);
    return false;
  }
}

bool RunComponentAndWait(async::Loop* loop, sys::ComponentContext* context, const std::string& app,
                         const std::vector<std::string>& args,
                         std::unique_ptr<fuchsia::sys::FlatNamespace> flat_namespace) {
  fuchsia::sys::ComponentControllerPtr component_controller;
  if (!RunComponent(context, app, args, std::move(flat_namespace), &component_controller)) {
    return false;
  }

  int64_t return_code;
  if (!WaitAndGetReturnCode(app, loop, &component_controller, &return_code)) {
    return false;
  }

  return return_code == 0;
}

}  // namespace test
}  // namespace tracing
