// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tests/run_test.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "garnet/bin/trace/options.h"
#include "garnet/bin/trace/tests/component_context.h"
#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "src/developer/tracing/lib/test_utils/run_program.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace tracing {
namespace test {

// The "path" of the trace program from outside the trace package.
const char kTraceProgramUrl[] = "fuchsia-pkg://fuchsia.com/trace#meta/trace.cmx";
// The path of the trace program as a shell command.
const char kTraceProgramPath[] = "/bin/trace";

static bool BuildTraceProgramArgs(const std::string& app_path, const std::string& test_name,
                                  const std::string& categories, size_t buffer_size_in_mb,
                                  const std::string& buffering_mode,
                                  std::initializer_list<std::string> additional_arguments,
                                  const std::string& relative_output_file_path,
                                  const syslog::LogSettings& log_settings,
                                  std::vector<std::string>* args) {
  AppendLoggingArgs(args, "", log_settings);
  args->push_back("record");

  args->push_back(fxl::StringPrintf("--buffer-size=%zu", buffer_size_in_mb));
  args->push_back(fxl::StringPrintf("--buffering-mode=%s", buffering_mode.c_str()));

  args->push_back(fxl::StringPrintf("--categories=%s", categories.c_str()));
  args->push_back(fxl::StringPrintf(
      "--output-file=%s",
      (std::string(kSpawnedTestTmpPath) + "/" + relative_output_file_path).c_str()));
  args->insert(args->end(), additional_arguments);

  AppendLoggingArgs(args, "--append-args=", log_settings);
  args->push_back(fxl::StringPrintf("--append-args=run,%s,%zu,%s", test_name.c_str(),
                                    buffer_size_in_mb, buffering_mode.c_str()));

  args->push_back(app_path);

  return true;
}

static bool BuildVerificationProgramArgs(const std::string& test_name, size_t buffer_size_in_mb,
                                         const std::string& buffering_mode,
                                         const std::string& output_file_path,
                                         const syslog::LogSettings& log_settings,
                                         std::vector<std::string>* args) {
  AppendLoggingArgs(args, "", log_settings);
  args->push_back("verify");
  args->push_back(test_name);
  args->push_back(fxl::StringPrintf("%zu", buffer_size_in_mb));
  args->push_back(buffering_mode);
  args->push_back(output_file_path);
  return true;
}

static zx_status_t AddAuxDirToSpawnAction(const char* local_path, const char* remote_path,
                                          fdio_spawn_action_t* actions) {
  zx::channel dir, server;

  zx_status_t status = zx::channel::create(0, &dir, &server);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not create channel aux directory";
    return false;
  }

  status = fdio_open(local_path, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, server.release());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not open " << local_path;
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

  FX_CHECK(num_actions <= std::size(spawn_actions));

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
    FX_LOGS(ERROR) << "trace exited with return code " << return_code;
    return false;
  }

  return true;
}

static bool AddAuxDirToLaunchInfo(const char* local_path, const char* remote_path,
                                  fuchsia::sys::FlatNamespace* flat_namespace) {
  zx::channel dir, server;

  zx_status_t status = zx::channel::create(0, &dir, &server);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not create channel aux directory";
    return false;
  }

  status = fdio_open(local_path, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, server.release());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not open " << local_path;
    return false;
  }

  flat_namespace->paths.push_back(remote_path);
  flat_namespace->directories.push_back(std::move(dir));
  return true;
}

static bool RunTraceComponentAndWait(const std::string& app, const std::vector<std::string>& args) {
  auto flat_namespace = std::make_unique<fuchsia::sys::FlatNamespace>();
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

bool RunIntegrationTest(const std::string& app_path, const std::string& test_name,
                        const std::string& categories, size_t buffer_size_in_mb,
                        const std::string& buffering_mode,
                        std::initializer_list<std::string> additional_arguments,
                        const std::string& relative_output_file_path,
                        const syslog::LogSettings& log_settings) {
  std::vector<std::string> args;
  BuildTraceProgramArgs(app_path, test_name, categories, buffer_size_in_mb, buffering_mode,
                        additional_arguments, relative_output_file_path, log_settings, &args);

  FX_LOGS(INFO) << "Running test " << test_name << " with " << buffer_size_in_mb << " MB "
                << buffering_mode << " buffer, tracing categories " << categories
                << ", output file " << relative_output_file_path;

  return RunTraceComponentAndWait(kTraceProgramUrl, args);
}

bool VerifyIntegrationTest(const std::string& app_path, const std::string& test_name,
                           size_t buffer_size_in_mb, const std::string& buffering_mode,
                           const std::string& relative_output_file_path,
                           const syslog::LogSettings& log_settings) {
  std::vector<std::string> args;
  BuildVerificationProgramArgs(test_name, buffer_size_in_mb, buffering_mode,
                               std::string(kSpawnedTestTmpPath) + "/" + relative_output_file_path,
                               log_settings, &args);

  FX_LOGS(INFO) << "Verifying test " << test_name << " with " << buffer_size_in_mb << " MB "
                << buffering_mode << " buffer, output file " << relative_output_file_path;

  return RunTraceComponentAndWait(app_path, args);
}

}  // namespace test
}  // namespace tracing
