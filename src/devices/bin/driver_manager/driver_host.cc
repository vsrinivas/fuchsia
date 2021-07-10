// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_host.h"

#include <lib/fdio/spawn-actions.h>
#include <lib/fdio/spawn.h>
#include <zircon/status.h>

#include <vector>

#include "coordinator.h"
#include "fdio.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"

DriverHost::DriverHost(Coordinator* coordinator,
                       fidl::ClientEnd<fuchsia_device_manager::DevhostController> controller,
                       fidl::ClientEnd<fuchsia_io::Directory> diagnostics, zx::process proc)
    : coordinator_(coordinator), proc_(std::move(proc)) {
  if (controller.is_valid()) {
    controller_.Bind(std::move(controller), coordinator_->dispatcher());
  }
  // cache the process's koid
  zx_info_handle_basic_t info;
  if (proc_.is_valid() &&
      proc_.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) == ZX_OK) {
    koid_ = info.koid;
  }

  coordinator_->RegisterDriverHost(this);
  driver_host_dir_ = coordinator_->inspect_manager().driver_host_dir();
  if (diagnostics.is_valid()) {
    driver_host_dir_->AddEntry(std::to_string(koid_),
                               fbl::MakeRefCounted<fs::RemoteDir>(std::move(diagnostics)));
  }
}

DriverHost::~DriverHost() {
  coordinator_->UnregisterDriverHost(this);
  driver_host_dir_->RemoveEntry(std::to_string(koid_));
  proc_.kill();
  LOGF(INFO, "Destroyed driver_host %p", this);
}

zx_status_t DriverHost::Launch(const DriverHostConfig& config, fbl::RefPtr<DriverHost>* out) {
  auto dh_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DevhostController>();
  if (dh_endpoints.is_error()) {
    return dh_endpoints.error_value();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }

  // Give driver_hosts the root resource if we have it (in tests, we may not)
  // TODO: limit root resource to root driver_host only
  zx::resource resource;
  if (config.root_resource->is_valid()) {
    zx_status_t status = config.root_resource->duplicate(ZX_RIGHT_SAME_RIGHTS, &resource);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to duplicate root resource: %s", zx_status_get_string(status));
    }
  }

  FdioSpawnActions fdio_spawn_actions;
  fdio_spawn_actions.AddAction(
      fdio_spawn_action_t{.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = config.name}});

  auto fs_object = config.fs_provider->CloneFs("driver_host_svc");
  fdio_spawn_actions.AddActionWithNamespace(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns = {.prefix = "/svc"},
      },
      fs_object.TakeChannel());

  fdio_spawn_actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_HND(PA_USER0, 0)},
      },
      dh_endpoints->server.TakeChannel());

  if (resource.is_valid()) {
    fdio_spawn_actions.AddActionWithHandle(
        fdio_spawn_action_t{
            .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
            .h = {.id = PA_HND(PA_RESOURCE, 0)},
        },
        std::move(resource));
  }

  uint32_t flags = FDIO_SPAWN_CLONE_ENVIRON | FDIO_SPAWN_CLONE_STDIO | FDIO_SPAWN_CLONE_UTC_CLOCK;
  if (!*config.loader_service_connector) {
    flags |= FDIO_SPAWN_DEFAULT_LDSVC;
  } else {
    zx::channel loader_service_client;
    zx_status_t status = (*config.loader_service_connector)(&loader_service_client);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed connect to loader service: %s", zx_status_get_string(status));
      return status;
    }

    fdio_spawn_actions.AddActionWithHandle(
        fdio_spawn_action_t{
            .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
            .h = {.id = PA_HND(PA_LDSVC_LOADER, 0)},
        },
        std::move(loader_service_client));
  }

  fdio_spawn_actions.AddActionWithHandle(
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h = {.id = PA_DIRECTORY_REQUEST},
      },
      endpoints->server.TakeChannel());

  zx::process proc;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  const char* const argv[] = {config.binary, nullptr};
  std::vector<fdio_spawn_action_t> actions = fdio_spawn_actions.GetActions();
  zx_status_t status =
      fdio_spawn_etc(config.job->get(), flags, argv[0], argv, config.env, actions.size(),
                     actions.data(), proc.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to launch driver_host '%s': %s", config.name, err_msg);
    return status;
  }

  auto host = fbl::MakeRefCounted<DriverHost>(config.coordinator, std::move(dh_endpoints->client),
                                              std::move(endpoints->client), std::move(proc));
  LOGF(INFO, "Launching driver_host '%s' (pid %zu)", config.name, host->koid());
  *out = std::move(host);
  return ZX_OK;
}
