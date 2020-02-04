// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tests/run_test.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
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
#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "src/developer/tracing/lib/test_utils/run_program.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/arraysize.h"
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
  args->push_back(
      fxl::StringPrintf("--append-args=run,%s",
                        (std::string(spec.spawn ? kSpawnedTestPackagePath : kTestPackagePath) +
                         "/" + relative_tspec_path)
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

bool RunTrace(const zx::job& job, const std::vector<std::string>& args, zx::process* out_child) {
  std::vector<std::string> argv{kTraceProgramPath};
  for (const auto& arg : args) {
    argv.push_back(arg);
  }

  size_t num_actions = 0;
  fdio_spawn_action_t spawn_actions[2];

  // Add a path to our /pkg so trace can read, e.g., tspec files.
  zx_status_t status = AddAuxDirToSpawnAction(kTestPackagePath, kSpawnedTestPackagePath,
                                              &spawn_actions[num_actions++]);
  if (status != ZX_OK) {
    return false;
  }
  // Add a path to our /tmp so trace can write, e.g., trace files there.
  status = AddAuxDirToSpawnAction(kTestTmpPath, kSpawnedTestTmpPath, &spawn_actions[num_actions++]);
  if (status != ZX_OK) {
    return false;
  }

  FXL_CHECK(num_actions <= arraysize(spawn_actions));

  return RunProgram(job, argv, num_actions, spawn_actions, out_child) == ZX_OK;
}

bool RunTraceAndWait(const zx::job& job, const std::vector<std::string>& args) {
  zx::process subprocess;
  if (!RunTrace(job, args, &subprocess)) {
    return false;
  }

  int64_t return_code;
  if (!WaitAndGetReturnCode("trace", subprocess, &return_code)) {
    return false;
  }
  if (return_code != 0) {
    FXL_LOG(ERROR) << "trace exited with return code " << return_code;
    return false;
  }

  return true;
}

static bool AddAuxDirToLaunchInfo(const char* local_path, const char* remote_path,
                                  fuchsia::sys::FlatNamespace* flat_namespace) {
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

  flat_namespace->paths.push_back(remote_path);
  flat_namespace->directories.push_back(std::move(dir));
  return true;
}

static bool RunTraceComponentAndWait(const std::string& app, const std::vector<std::string>& args) {
  auto flat_namespace = fuchsia::sys::FlatNamespace::New();
  // Add a path to our /pkg so trace can read tspec files.
  if (!AddAuxDirToLaunchInfo(kTestPackagePath, kSpawnedTestPackagePath, flat_namespace.get())) {
    return false;
  }
  // Add a path to our /tmp so trace can write trace files there.
  if (!AddAuxDirToLaunchInfo(kTestTmpPath, kSpawnedTestTmpPath, flat_namespace.get())) {
    return false;
  }

  async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};
  sys::ComponentContext* context = tracing::test::GetComponentContext();
  return RunComponentAndWait(&loop, context, app, args, std::move(flat_namespace));
}

bool RunTspec(const std::string& relative_tspec_path,
              const std::string& relative_output_file_path) {
  std::vector<std::string> args;
  if (!BuildTraceProgramArgs(relative_tspec_path, relative_output_file_path, &args)) {
    return false;
  }

  FXL_LOG(INFO) << "Running tspec " << relative_tspec_path << ", output file "
                << relative_output_file_path;

  return RunTraceComponentAndWait(kTraceProgramUrl, args);
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
      (std::string(spec.spawn ? kSpawnedTestPackagePath : kTestPackagePath) + "/" +
       relative_tspec_path),
      std::string(kSpawnedTestTmpPath) + "/" + relative_output_file_path, &args);

  FXL_LOG(INFO) << "Verifying tspec " << relative_tspec_path << ", output file "
                << relative_output_file_path;

  // For consistency we do the exact same thing that the trace program does.
  if (spec.spawn) {
    zx::job job{};  // -> default job
    std::vector<std::string> argv{program_path};
    for (const auto& arg : args) {
      argv.push_back(arg);
    }
    return RunProgramAndWait(job, argv, 0, nullptr);
  } else {
    return RunTraceComponentAndWait(program_path, args);
  }
}

}  // namespace test
}  // namespace tracing
