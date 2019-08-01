// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/cpp/fidl.h>
#include <fuchsia/device/cpp/fidl.h>
#include <fuchsia/device/manager/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/paver/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/virtualconsole/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/service_directory.h>
#include <trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/appmgr/appmgr.h"
#include "src/lib/fxl/command_line.h"

namespace {

std::vector<std::string> RootRealmServices() {
  return std::vector<std::string>{
      fuchsia::boot::FactoryItems::Name_,
      fuchsia::boot::RootJob::Name_,
      fuchsia::boot::RootResource::Name_,
      fuchsia::device::NameProvider::Name_,
      fuchsia::device::manager::Administrator::Name_,
      fuchsia::device::manager::DebugDumper::Name_,
      fuchsia::kernel::Counter::Name_,
      fuchsia::kernel::DebugBroker::Name_,
      fuchsia::paver::Paver::Name_,
      fuchsia::scheduler::ProfileProvider::Name_,
      fuchsia::virtualconsole::SessionManager::Name_,
  };
}

}  // namespace

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);

  auto environment_services = sys::ServiceDirectory::CreateFromNamespace();

  // Certain services in appmgr's /svc, which is served by svchost, are added to
  // the root realm so they can be routed into a nested environment (such as the
  // sys realm in sysmgr) and used in components.
  fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);
  root_realm_services->names = RootRealmServices();
  root_realm_services->host_directory = environment_services->CloneChannel().TakeChannel();

  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  component::AppmgrArgs args{.pa_directory_request = std::move(request),
                             .root_realm_services = std::move(root_realm_services),
                             .environment_services = std::move(environment_services),
                             .sysmgr_url = "fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx",
                             .sysmgr_args = {},
                             .run_virtual_console = true,
                             .retry_sysmgr_crash = true};
  component::Appmgr appmgr(loop.dispatcher(), std::move(args));

  loop.Run();
  return 0;
}
