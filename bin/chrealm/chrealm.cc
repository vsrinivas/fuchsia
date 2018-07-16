// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/chrealm/chrealm.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/util.h>
#include <lib/zx/job.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/strings/concatenate.h"

namespace chrealm {

zx_status_t RunBinaryInRealm(const std::string& realm_path, const char** argv,
                             int64_t* return_code) {
  *return_code = -1;
  fdio_ns_t* ns = nullptr;
  fdio_flat_namespace_t* flat_ns = nullptr;
  auto cleanup = fxl::MakeAutoCall([&]() {
    if (ns != nullptr) {
      fdio_ns_destroy(ns);
    }
    free(flat_ns);
  });

  // Get the process's namespace.
  zx_status_t status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not obtain namespace\n");
    return status;
  }

  // Open the provided path, which is the realm's hub directory.
  zx_handle_t realm_hub_dir = ZX_HANDLE_INVALID;
  status = fdio_ns_open(ns, realm_path.c_str(), ZX_FS_RIGHT_READABLE,
                        &realm_hub_dir);
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not open hub in realm: %s\n",
            realm_path.c_str());
    return status;
  }

  // Open the services dir in the realm's hub directory.
  zx_handle_t realm_svc_dir = ZX_HANDLE_INVALID;
  const std::string svc_path = fxl::Concatenate({realm_path, "/svc"});
  status =
      fdio_ns_open(ns, svc_path.c_str(), ZX_FS_RIGHT_READABLE, &realm_svc_dir);
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not open svc in realm: %s\n",
            svc_path.c_str());
    return status;
  }

  // Open the realm's job.
  fuchsia::sys::JobProviderSyncPtr job_provider;
  status = fdio_service_connect_at(
      realm_hub_dir, "job", job_provider.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not connect to job provider: %s\n",
            svc_path.c_str());
    return status;
  }
  zx::job realm_job;
  status = job_provider->GetJob(&realm_job);
  if (status == ZX_OK && !realm_job) {
    status = ZX_ERR_INTERNAL;
  }
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not get realm job\n");
    return status;
  }

  // Convert 'ns' to a flat namespace and replace /svc and /hub.
  status = fdio_ns_export(ns, &flat_ns);
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not flatten namespace\n");
    return status;
  }
  fdio_spawn_action_t actions[flat_ns->count];
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

  // Launch the binary.
  const char* binary_path = argv[0];
  const uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_NAMESPACE;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t proc = ZX_HANDLE_INVALID;
  status = fdio_spawn_etc(realm_job.release(), flags, binary_path, argv,
                          nullptr, countof(actions), actions, &proc, err_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "error: failed to launch command: %s\n", err_msg);
    return status;
  }

  status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE,
                              nullptr);
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not wait for command\n");
    return status;
  }
  zx_info_process_t info;
  status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info),
                              nullptr, nullptr);
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not get result of command\n");
    return status;
  }
  *return_code = info.return_code;
  return ZX_OK;
}

}  // namespace chrealm