// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sysinfo.h"

#include <fcntl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <fbl/unique_fd.h>

namespace sysinfo {
void SysInfo::GetHypervisorResource(GetHypervisorResourceCompleter::Sync completer) {
  zx::resource hypervisor;
  zx_status_t status = GetHypervisorResource(&hypervisor);
  return completer.Reply(status, std::move(hypervisor));
}

void SysInfo::GetBoardName(GetBoardNameCompleter::Sync completer) {
  std::string board_name;
  zx_status_t status = GetBoardName(&board_name);
  return completer.Reply(status, fidl::StringView(board_name));
}

void SysInfo::GetBoardRevision(GetBoardRevisionCompleter::Sync completer) {
  uint32_t revision;
  zx_status_t status = GetBoardRevision(&revision);
  return completer.Reply(status, revision);
}

void SysInfo::GetInterruptControllerInfo(GetInterruptControllerInfoCompleter::Sync completer) {
  ::llcpp::fuchsia::sysinfo::InterruptControllerInfo info = {};
  zx_status_t status = GetInterruptControllerInfo(&info);
  return completer.Reply(status, &info);
}

// TODO(43777): Separate out GetHypervisorResource from sysinfo
zx_status_t SysInfo::GetHypervisorResource(zx::resource *hypervisor) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    printf("sysinfo: channel create failed: %s\n", zx_status_get_string(status));
    return status;
  }
  auto svc_name = "/svc/" + std::string(::llcpp::fuchsia::boot::RootResource::Name);
  status = fdio_service_connect(svc_name.c_str(), remote.release());
  if (status != ZX_OK) {
    printf("sysinfo: Could not connect to RootResource service: %s\n",
           zx_status_get_string(status));
    return status;
  }

  ::llcpp::fuchsia::boot::RootResource::SyncClient client(std::move(local));
  auto result = client.Get();
  if (result.status() != ZX_OK) {
    printf("sysinfo: Could not retrieve RootResource: %s\n", zx_status_get_string(result.status()));
    return result.status();
  }
  zx::resource root_resource(std::move(result->resource));

  const char name[] = "hypervisor";
  status = zx::resource::create(root_resource, ZX_RSRC_KIND_HYPERVISOR, 0, 0, name, sizeof(name),
                                hypervisor);

  return status;
}

zx_status_t SysInfo::GetBoardName(std::string *board_name) {
  zx::channel local;
  zx_status_t status = ConnectToPBus(&local);
  if (status != ZX_OK) {
    printf("sysinfo: Could not connect to platform bus: %s\n", zx_status_get_string(status));
    return status;
  }
  ::llcpp::fuchsia::sysinfo::SysInfo::SyncClient client(std::move(local));

  // Get board name from platform bus
  auto result = client.GetBoardName();
  if (!result.ok()) {
    printf("sysinfo: Could not GetBoardName: %s\n", result.error());
    return result.status();
  }
  *board_name = std::string(result->name.cbegin(), result->name.size());

  return status;
}

zx_status_t SysInfo::GetBoardRevision(uint32_t *board_revision) {
  zx::channel local;
  zx_status_t status = ConnectToPBus(&local);
  if (status != ZX_OK) {
    printf("sysinfo: Could not connect to platform bus: %s\n", zx_status_get_string(status));
    return status;
  }

  ::llcpp::fuchsia::sysinfo::SysInfo::SyncClient client(std::move(local));

  // Get board revision from platform bus
  auto result = client.GetBoardRevision();
  if (!result.ok()) {
    printf("sysinfo: Could not GetBoardRevision: %s\n", result.error());
    return result.status();
  }
  *board_revision = result->revision;

  return status;
}

zx_status_t SysInfo::GetInterruptControllerInfo(
    ::llcpp::fuchsia::sysinfo::InterruptControllerInfo *info) {
  zx::channel local;
  zx_status_t status = ConnectToPBus(&local);
  if (status != ZX_OK) {
    printf("sysinfo: Could not connect to platform bus: %s\n", zx_status_get_string(status));
    return status;
  }
  ::llcpp::fuchsia::sysinfo::SysInfo::SyncClient client(std::move(local));

  // Get interrupt controller information from platform bus
  auto result = client.GetInterruptControllerInfo();
  if (!result.ok()) {
    printf("sysinfo: Could not GetInterruptControllerInfo: %s\n", result.error());
    return result.status();
  }
  info->type = result->info->type;
  return status;
}

zx_status_t SysInfo::ConnectToPBus(zx::channel *local) {
  zx::channel remote;
  zx_status_t status = zx::channel::create(0, local, &remote);
  if (status != ZX_OK) {
    printf("sysinfo: channel create failed: %s\n", zx_status_get_string(status));
    return status;
  }

  status = fdio_service_connect("/dev/sys/platform", remote.release());
  if (status != ZX_OK) {
    printf("sysinfo: Could not connect to pbus: %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

}  // namespace sysinfo
