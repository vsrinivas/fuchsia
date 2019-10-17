// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tests/run_test.h"

#include <fbl/algorithm.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <string>
#include <vector>

#include "garnet/bin/trace/spec.h"
#include "garnet/bin/trace/tests/component_context.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fxl/log_settings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace tracing {
namespace test {

// The "path" of the trace program from outside the trace package.
const char kTraceProgramUrl[] = "fuchsia-pkg://fuchsia.com/trace#meta/trace.cmx";
// The path of the trace program as a shell command.
const char kTraceProgramPath[] = "/bin/trace";
// The path of the trace program from within the trace package.
// const char kTracePackageProgramPath[] = "/pkg/bin/trace";

void AppendLoggingArgs(std::vector<std::string>* argv, const char* prefix) {
  // Transfer our log settings to the subprogram.
  auto log_settings = fxl::GetLogSettings();
  std::string log_file_arg;
  std::string verbose_or_quiet_arg;
  if (log_settings.log_file != "") {
    log_file_arg = fxl::StringPrintf("%s--log-file=%s", prefix, log_settings.log_file.c_str());
    argv->push_back(log_file_arg);
  }
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

static bool BuildTraceProgramArgs(const std::string& relative_tspec_path,
                                  const std::string& relative_output_file_path,
                                  std::vector<std::string>* args) {
  tracing::Spec spec;
  if (!ReadTspec(std::string(kTestPackagePath) + "/" + relative_tspec_path, &spec)) {
    return false;
  }

  AppendLoggingArgs(args, "");
  args->push_back("record");
  args->push_back(fxl::StringPrintf(
                      "--spec-file=%s",
                      (std::string(kSpawnedTestPackagePath) + "/" + relative_tspec_path).c_str()));
  args->push_back(fxl::StringPrintf(
                      "--output-file=%s",
                      (std::string(kSpawnedTestTmpPath) + "/" + relative_output_file_path).c_str()));

  AppendLoggingArgs(args, "--append-args=");

  // Note that |relative_tspec_path| cannot have a comma.
  args->push_back(fxl::StringPrintf(
      "--append-args=run,%s",
      (std::string(spec.spawn ? kSpawnedTestPackagePath : kTestPackagePath) + "/" + relative_tspec_path)
          .c_str()));

  return true;
}

static void BuildVerificationProgramArgs(const std::string& tspec_path,
                                         const std::string& output_file_path,
                                         std::vector<std::string>* args) {
  AppendLoggingArgs(args, "");

  args->push_back("verify");
  args->push_back(tspec_path);
  args->push_back(output_file_path);
}

static zx_status_t AddAuxDirToSpawnAction(const char* local_path, const char* remote_path,
                                          fdio_spawn_action_t* actions) {
  zx::channel dir, server;

  zx_status_t status = zx::channel::create(0, &dir, &server);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Could not create channel aux directory";
    return false;
  }

  status = fdio_open(local_path, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, server.release());
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Could not open " << local_path;
    return false;
  }

  actions->action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY;
  actions->ns.prefix = remote_path;
  actions->ns.handle = dir.release();
  return ZX_OK;
}

zx_status_t SpawnProgram(const zx::job& job, const std::vector<std::string>& argv,
                         zx_handle_t arg_handle, zx::process* out_process) {
  std::vector<const char*> c_argv;
  StringArgvToCArgv(argv, &c_argv);

  FXL_VLOG(1) << "Running " << fxl::JoinStrings(argv, " ");

  size_t action_count = 0;
  fdio_spawn_action_t spawn_actions[3];

  if (arg_handle != ZX_HANDLE_INVALID) {
    spawn_actions[action_count].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    spawn_actions[action_count].h.id = PA_HND(PA_USER0, 0);
    spawn_actions[action_count].h.handle = arg_handle;
    ++action_count;
  }

  // Add a path to our /pkg so trace can read tspec files.
  zx_status_t status = AddAuxDirToSpawnAction(kTestPackagePath, kSpawnedTestPackagePath,
                                              &spawn_actions[action_count++]);
  if (status != ZX_OK) {
    return status;
  }
  // Add a path to our /tmp so trace can write trace files there.
  status = AddAuxDirToSpawnAction(kTestTmpPath, kSpawnedTestTmpPath,
                                  &spawn_actions[action_count++]);
  if (status != ZX_OK) {
    return status;
  }

  FXL_CHECK(action_count <= fbl::count_of(spawn_actions));

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  status = fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, c_argv[0], c_argv.data(), nullptr,
                          action_count, &spawn_actions[0],
                          out_process->reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Spawning " << c_argv[0] << " failed: " << err_msg << ", " << status;
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
    FXL_PLOG(ERROR, status) << "Failed waiting for program " << program_name << " to exit";
    return false;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(process.get(), ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                              nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Error getting return code for program " << program_name;
    return false;
  }

  if (proc_info.return_code != 0) {
    FXL_LOG(INFO) << program_name << " exited with return code " << proc_info.return_code;
  }
  *out_return_code = proc_info.return_code;
  return true;
}

static bool RunProgramAndWait(const zx::job& job, const std::vector<std::string>& argv) {
  zx::process subprocess;

  auto status = SpawnProgram(job, argv, ZX_HANDLE_INVALID, &subprocess);
  if (status != ZX_OK) {
    return false;
  }

  int64_t return_code;
  if (!WaitAndGetReturnCode(argv[0], subprocess, &return_code)) {
    return false;
  }
  if (return_code != 0) {
    FXL_LOG(ERROR) << argv[0] << " exited with return code " << return_code;
    return false;
  }

  return true;
}

bool RunTrace(const zx::job& job, const std::vector<std::string>& args, zx::process* out_child) {
  std::vector<std::string> argv{kTraceProgramPath};
  for (const auto& arg : args) {
    argv.push_back(arg);
  }
  return SpawnProgram(job, argv, ZX_HANDLE_INVALID, out_child) == ZX_OK;
}

bool RunTraceAndWait(const zx::job& job, const std::vector<std::string>& args) {
  std::vector<std::string> argv{kTraceProgramPath};
  for (const auto& arg : args) {
    argv.push_back(arg);
  }
  return RunProgramAndWait(job, argv);
}

static bool AddAuxDirToLaunchInfo(const char* local_path, const char* remote_path,
                                  fuchsia::sys::LaunchInfo* launch_info) {
  zx::channel dir, server;

  zx_status_t status = zx::channel::create(0, &dir, &server);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Could not create channel aux directory";
    return false;
  }

  status = fdio_open(local_path, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, server.release());
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Could not open " << local_path;
    return false;
  }

  launch_info->flat_namespace->paths.push_back(remote_path);
  launch_info->flat_namespace->directories.push_back(std::move(dir));
  return true;
}

static bool RunComponent(sys::ComponentContext* context, const std::string& app,
                         const std::vector<std::string>& args,
                         fuchsia::sys::ComponentControllerPtr* component_controller) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = std::string(app);
  launch_info.arguments = fidl::To<fidl::VectorPtr<std::string>>(args);

  launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
  // Add a path to our /pkg so trace can read tspec files.
  if (!AddAuxDirToLaunchInfo(kTestPackagePath, kSpawnedTestPackagePath, &launch_info)) {
    return false;
  }
  // Add a path to our /tmp so trace can write trace files there.
  if (!AddAuxDirToLaunchInfo(kTestTmpPath, kSpawnedTestTmpPath, &launch_info)) {
    return false;
  }

  FXL_LOG(INFO) << "Launching: " << launch_info.url << " " << fxl::JoinStrings(args, " ");

  fuchsia::sys::LauncherPtr launcher;
  zx_status_t status = context->svc()->Connect(launcher.NewRequest());
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "context->svc()->Connect() failed for " << app;
    return false;
  }

  launcher->CreateComponent(std::move(launch_info), component_controller->NewRequest());
  return true;
}

static bool WaitAndGetReturnCode(const std::string& program_name, async::Loop* loop,
                                 fuchsia::sys::ComponentControllerPtr* component_controller,
                                 int64_t* out_return_code) {
  fuchsia::sys::TerminationReason termination_reason =
      fuchsia::sys::TerminationReason::INTERNAL_ERROR;
  // This value is not valid unless |termination_reason==EXITED|.
  int64_t return_code = INT64_MIN;

  component_controller->set_error_handler(
      [&loop, &program_name, &termination_reason](zx_status_t error) {
        FXL_PLOG(ERROR, error) << "Unexpected error waiting for " << program_name << " to exit";
        termination_reason = fuchsia::sys::TerminationReason::UNKNOWN;
        loop->Quit();
      });
  component_controller->events().OnTerminated =
      [&loop, &program_name, &component_controller, &termination_reason, &return_code](
          int64_t rc, fuchsia::sys::TerminationReason reason) {
        FXL_LOG(INFO) << "Component " << program_name << " exited with return reason/code "
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
    FXL_LOG(INFO) << program_name << ": return code " << return_code;
    *out_return_code = return_code;
    return true;
  } else {
    FXL_LOG(ERROR) << program_name << ": termination reason "
                   << static_cast<int>(termination_reason);
    return false;
  }
}

static bool RunComponentAndWait(async::Loop* loop, sys::ComponentContext* context,
                                const std::string& app, const std::vector<std::string>& args) {
  fuchsia::sys::ComponentControllerPtr component_controller;
  if (!RunComponent(context, app, args, &component_controller)) {
    return false;
  }

  int64_t return_code;
  if (!WaitAndGetReturnCode(app, loop, &component_controller, &return_code)) {
    return false;
  }

  return return_code == 0;
}

bool RunTspec(const std::string& relative_tspec_path,
              const std::string& relative_output_file_path) {
  std::vector<std::string> args;
  if (!BuildTraceProgramArgs(relative_tspec_path, relative_output_file_path, &args)) {
    return false;
  }

  FXL_LOG(INFO) << "Running tspec " << relative_tspec_path << ", output file "
                << relative_output_file_path;

  async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};
  sys::ComponentContext* context = tracing::test::GetComponentContext();
  return RunComponentAndWait(&loop, context, kTraceProgramUrl, args);
}

bool VerifyTspec(const std::string& relative_tspec_path,
                 const std::string& relative_output_file_path) {
  tracing::Spec spec;
  if (!ReadTspec(std::string(kTestPackagePath) + "/" + relative_tspec_path, &spec)) {
    return false;
  }

  FXL_DCHECK(spec.app);
  const std::string& program_path = *spec.app;

  std::vector<std::string> args;
  BuildVerificationProgramArgs(
      (std::string(spec.spawn ? kSpawnedTestPackagePath : kTestPackagePath) + "/" + relative_tspec_path),
      std::string(kTestTmpPath) + "/" + relative_output_file_path, &args);

  FXL_LOG(INFO) << "Verifying tspec " << relative_tspec_path << ", output file "
                << relative_output_file_path;

  // For consistency we do the exact same thing that the trace program does.
  if (spec.spawn) {
    zx::job job{};  // -> default job
    std::vector<std::string> argv{program_path};
    for (const auto& arg : args) {
      argv.push_back(arg);
    }
    return RunProgramAndWait(job, argv);
  } else {
    async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};
    sys::ComponentContext* context = tracing::test::GetComponentContext();
    return RunComponentAndWait(&loop, context, program_path, args);
  }
}

}  // namespace test
}  // namespace tracing
