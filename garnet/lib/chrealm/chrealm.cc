// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/chrealm/chrealm.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fit/defer.h>
#include <lib/zx/job.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "lib/fidl/cpp/interface_request.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace chrealm {

zx_status_t RunBinaryInRealm(const std::string& realm_path, const char** argv,
                             int64_t* return_code, std::string* error) {
  *return_code = -1;
  zx_handle_t proc;
  zx_status_t status = SpawnBinaryInRealmAsync(
      realm_path, argv, ZX_HANDLE_INVALID,
      FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_NAMESPACE, {}, &proc, error);
  if (status != ZX_OK) {
    return status;
  }
  status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE,
                              nullptr);
  if (status != ZX_OK) {
    *error = "Could not wait for command";
    return status;
  }
  zx_info_process_t info;
  status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info),
                              nullptr, nullptr);
  if (status != ZX_OK) {
    *error = "Could not get result of command";
    return status;
  }

  *return_code = info.return_code;
  return ZX_OK;
}

zx_status_t SpawnBinaryInRealmAsync(
    const std::string& realm_path, const char** argv, zx_handle_t job,
    int32_t flags, const std::vector<fdio_spawn_action_t>& additional_actions,
    zx_handle_t* proc, std::string* error) {
  if (flags & FDIO_SPAWN_CLONE_NAMESPACE) {
    *error = "chrealm does not support FDIO_SPAWN_CLONE_NAMESPACE";
    return ZX_ERR_INVALID_ARGS;
  }
  error->clear();
  // Get the process's namespace.
  fdio_ns_t* ns = nullptr;
  zx_status_t status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    *error = "Could not obtain namespace";
    return status;
  }

  // Open the provided path, which is the realm's hub directory.
  zx_handle_t realm_hub_dir = ZX_HANDLE_INVALID;
  status = fdio_ns_open(ns,
                        realm_path.c_str(),
                        ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE,
                        &realm_hub_dir);
  if (status != ZX_OK) {
    *error = fxl::StringPrintf("Could not open hub in realm: %s",
                               realm_path.c_str());
    return status;
  }

  // Open the services dir in the realm's hub directory.
  zx_handle_t realm_svc_dir = ZX_HANDLE_INVALID;
  const std::string svc_path = fxl::Concatenate({realm_path, "/svc"});
  status = fdio_ns_open(ns,
                        svc_path.c_str(),
                        ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE,
                        &realm_svc_dir);
  if (status != ZX_OK) {
    *error =
        fxl::StringPrintf("Could not open svc in realm: %s", svc_path.c_str());
    return status;
  }

  zx::job realm_job;
  if (!job) {
    // Open the realm's job.
    fuchsia::sys::JobProviderSyncPtr job_provider;
    status = fdio_service_connect_at(
        realm_hub_dir, "job",
        job_provider.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      *error = fxl::StringPrintf("Could not connect to job provider: %s",
                                 svc_path.c_str());
      return status;
    }
    status = job_provider->GetJob(&realm_job);
    if (status == ZX_OK && !realm_job) {
      status = ZX_ERR_INTERNAL;
    }
    if (status != ZX_OK) {
      *error = "Could not get realm job";
      return status;
    }
    job = realm_job.get();
  }

  // Convert 'ns' to a flat namespace and replace /svc and /hub.
  fdio_flat_namespace_t* flat_ns = nullptr;
  status = fdio_ns_export(ns, &flat_ns);
  if (status != ZX_OK) {
    *error = "Could not flatten namespace";
    return status;
  }
  auto cleanup = fit::defer([&flat_ns]() {
    fdio_ns_free_flat_ns(flat_ns);
  });

  size_t action_count = flat_ns->count + additional_actions.size();
  fdio_spawn_action_t actions[action_count];
  for (size_t i = 0; i < flat_ns->count; ++i) {
    zx_handle_t handle;
    if (std::string(flat_ns->path[i]) == "/svc") {
      handle = realm_svc_dir;
    } else if (std::string(flat_ns->path[i]) == "/hub") {
      handle = realm_hub_dir;
    } else {
      handle = flat_ns->handle[i];
    }
    fdio_spawn_action_t add_ns_entry = {
        .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
        .ns =
            {
                .prefix = flat_ns->path[i],
                .handle = handle,
            },
    };
    actions[i] = add_ns_entry;
  }
  for (size_t i = 0; i < additional_actions.size(); ++i) {
    actions[flat_ns->count + i] = additional_actions[i];
  }

  // Launch the binary.
  const char* binary_path = argv[0];
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  status =
      fdio_spawn_etc(job, flags & ~FDIO_SPAWN_CLONE_NAMESPACE, binary_path,
                     argv, nullptr, action_count, actions, proc, err_msg);
  if (status != ZX_OK) {
    *error = fxl::StringPrintf("Failed to launch command: %s", err_msg);
    return status;
  }

  return ZX_OK;
}

}  // namespace chrealm
