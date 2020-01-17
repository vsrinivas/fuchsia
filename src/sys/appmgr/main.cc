// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <src/lib/files/directory.h>
#include <trace-provider/provider.h>

#include "src/lib/fxl/command_line.h"
#include "src/sys/appmgr/appmgr.h"

namespace {

// Returns the set of service names that should be proxied to the root realm
// from appmgr's namespace
std::vector<std::string> RootRealmServices() {
  std::vector<std::string> res;
  bool success = files::ReadDirContents("/svc_for_sys", &res);
  if (!success) {
    FXL_LOG(WARNING) << "failed to read /svc_for_sys (" << errno
                     << "), not forwarding services to sys realm";
    return std::vector<std::string>();
  }
  return res;
}

}  // namespace

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);

  zx::channel svc_for_sys_server, svc_for_sys_client;
  zx_status_t status = zx::channel::create(0, &svc_for_sys_server, &svc_for_sys_client);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create channel: " << errno;
    return status;
  }
  status =
      fdio_open("/svc_for_sys", ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DIRECTORY | ZX_FS_RIGHT_WRITABLE,
                svc_for_sys_server.release());
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "failed to open /svc_for_sys (" << errno
                     << "), not forwarding services to sys realm";
    return status;
  }

  auto environment_services =
      std::make_shared<sys::ServiceDirectory>(std::move(svc_for_sys_client));

  // Certain services in appmgr's /svc, which is served by svchost, are added to
  // the root realm so they can be routed into a nested environment (such as the
  // sys realm in sysmgr) and used in components.
  fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);
  root_realm_services->names = RootRealmServices();
  root_realm_services->host_directory = environment_services->CloneChannel().TakeChannel();

  zx::channel trace_client, trace_server;
  status = zx::channel::create(0, &trace_client, &trace_server);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create tracing channel: " << status;
    return status;
  }

  trace::TraceProvider trace_provider(std::move(trace_client), loop.dispatcher());

  component::AppmgrArgs args{.pa_directory_request = std::move(request),
                             .root_realm_services = std::move(root_realm_services),
                             .environment_services = std::move(environment_services),
                             .sysmgr_url = "fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx",
                             .sysmgr_args = {},
                             .run_virtual_console = true,
                             .retry_sysmgr_crash = true,
                             .trace_server_channel = std::move(trace_server)};
  component::Appmgr appmgr(loop.dispatcher(), std::move(args));

  loop.Run();
  return 0;
}
