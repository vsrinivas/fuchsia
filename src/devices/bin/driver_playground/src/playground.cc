// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_playground/src/playground.h"

#include <fidl/fuchsia.process/cpp/wire.h>
#include <lib/fdio/spawn-actions.h>
#include <lib/fdio/spawn.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/status.h>
#include <zircon/processargs.h>

#include <iostream>

#include "src/devices/bin/driver_playground/src/playground_utils.h"
#include "src/devices/lib/log/log.h"

namespace fprocess = fuchsia_process;

namespace {

struct ResolvedProcess {
  zx::vmo executable;
  zx::channel ldsvc;
};

// This function is based on one in sdk/lib/fdio/spawn.cc
// ResolveName makes a call to the fuchsia.process.Resolver service and may return a vmo and
// associated loader service, if the name resolves within the current realm.
zx::status<ResolvedProcess> ResolveName(std::string_view name) {
  auto client = service::Connect<fprocess::Resolver>();
  if (client.is_error()) {
    return client.take_error();
  }
  auto resolver = fidl::BindSyncClient(*std::move(client));
  auto response = resolver->Resolve(fidl::StringView::FromExternal(name));

  zx_status_t status = response.status();
  if (status != ZX_OK) {
    LOGF(ERROR, "failed to send resolver request: %d (%s)", status, zx_status_get_string(status));
    return zx::error(ZX_ERR_INTERNAL);
  }

  status = response.value().status;
  if (status != ZX_OK) {
    LOGF(WARNING, "failed to resolve %.*s: %d (%s)", static_cast<int>(name.size()), name.data(),
         status, zx_status_get_string(status));
    return zx::error(status);
  }

  ResolvedProcess resolved = {.executable = std::move(response.value().executable),
                              .ldsvc = std::move(response.value().ldsvc.channel())};

  return zx::ok(std::move(resolved));
}

// This function is based on one from zircon/third_party/uapp/dash/src/process.c
// Check for process termination, this call blocks until the termination.
zx::status<int> ProcessAwaitTermination(zx::process& process) {
  zx_status_t status = process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr);
  if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
    return zx::error(status);
  }

  zx_info_process_t proc_info;
  status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);

  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(static_cast<int>(proc_info.return_code));
}

}  // namespace

void Playground::RunTool(RunToolRequestView request, RunToolCompleter::Sync& completer) {
  // This is the default url to be pre-pended to a tool if it is not an absolute url.
  // Makes running tools that are part of the playground package easy.
  constexpr std::string_view kDefaultPackageUrl =
      "fuchsia-pkg://fuchsia.com/driver_playground#bin/";

  std::string_view tool_name = request->tool.get();
  std::vector<std::string> str_argv = playground_utils::ExtractStringArgs(tool_name, request->args);
  std::vector<const char*> argv = playground_utils::ConvertToArgv(str_argv);
  std::string name_for_resolve = playground_utils::GetNameForResolve(kDefaultPackageUrl, tool_name);
  zx::status<ResolvedProcess> resolve_name_status = ResolveName(name_for_resolve);
  if (resolve_name_status.is_error()) {
    LOGF(ERROR, "failed to resolve name.");
    completer.ReplyError(resolve_name_status.error_value());
    return;
  }

  zx::job zx_job;
  zx_status_t job_create_status = zx::job::create(*zx::job::default_job(), 0, &zx_job);
  if (job_create_status != ZX_OK) {
    LOGF(ERROR, "Cannot create child process: %d (%s)", job_create_status,
         zx_status_get_string(job_create_status));
    completer.ReplyError(job_create_status);
    return;
  }

  uint32_t flags = FDIO_SPAWN_CLONE_ALL;
  flags &= ~FDIO_SPAWN_CLONE_ENVIRON;
  flags &= ~FDIO_SPAWN_DEFAULT_LDSVC;
  flags &= ~FDIO_SPAWN_CLONE_STDIO;

  FdioSpawnActions fdio_spawn_actions;
  fdio_spawn_actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_LDSVC_LOADER, 0)},
      },
      std::move(resolve_name_status->ldsvc));

  fdio_spawn_actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_FD, 0)},
      },
      std::move(request->stdio_params.standard_in()));

  fdio_spawn_actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_FD, 1)},
      },
      std::move(request->stdio_params.standard_out()));

  fdio_spawn_actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_FD, 2)},
      },
      std::move(request->stdio_params.standard_err()));

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx::process zx_process;
  std::vector<fdio_spawn_action_t> actions = fdio_spawn_actions.GetActions();
  zx_status_t spawn_status = fdio_spawn_vmo(
      zx_job.get(), flags, resolve_name_status->executable.release(), argv.data(), nullptr,
      actions.size(), actions.data(), zx_process.reset_and_get_address(), err_msg);
  if (spawn_status != ZX_OK) {
    LOGF(ERROR, "failed to fdio_spawn_vmo: %s\n", err_msg);
    completer.ReplyError(spawn_status);
    return;
  }

  completer.ReplySuccess();

  zx::status<int> termination_status = ProcessAwaitTermination(zx_process);

  if (termination_status.is_error()) {
    LOGF(ERROR, "failed to await termination.");
    completer.ReplyError(termination_status.error_value());
    return;
  }

  fidl::Status fidl_status =
      fidl::WireSendEvent(request->close_controller)->OnTerminated(termination_status.value());
  if (!fidl_status.ok()) {
    LOGF(WARNING, "Sending OnTerminated failed");
  }
}
