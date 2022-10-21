// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <src/lib/files/directory.h>

#include "src/lib/fxl/command_line.h"
#include "src/sys/appmgr/appmgr.h"
#include "src/sys/appmgr/moniker.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace {

// Returns the set of service names that should be proxied to the root realm
// from appmgr's namespace
std::vector<std::string> RootRealmServices() {
  std::vector<std::string> res;
  bool success = files::ReadDirContents("/svc_for_sys", &res);
  if (!success) {
    FX_LOGS(WARNING) << "failed to read /svc_for_sys (" << errno
                     << "), not forwarding services to sys realm";
    return std::vector<std::string>();
  }
  return res;
}

// Creates a zircon socket and sets it to appmgr's stdin.
zx_status_t InitStdinSocket() {
  zx::socket reader, writer;
  // Create a socket pair for stdin. We'll just discard the writer so stdin always looks like it's
  // closed.
  zx_status_t status = zx::socket::create(0, &writer, &reader);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create stdin socket: " << zx_status_get_string(status);
    return status;
  }
  status = reader.replace(ZX_RIGHTS_BASIC | ZX_RIGHT_READ, &reader);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to replace stdin reader: " << zx_status_get_string(status);
    return status;
  }
  fdio_t* io;
  status = fdio_create(reader.release(), &io);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create fdio struct for stdin reader: "
                   << zx_status_get_string(status);
  }
  int fd = fdio_bind_to_fd(io, STDIN_FILENO, STDIN_FILENO);
  if (fd != STDIN_FILENO) {
    FX_LOGS(ERROR) << "failed to bind socket to stdin";
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

}  // namespace

// Flag that allows overriding the "auto_update_packages" default set in GN. Useful for tests.
const char kAutoUpdatePackages[] = "auto_update_packages";
// Flag that determines whether sysmgr will be included.
const char kLaunchSysmgr[] = "launch_sysmgr";

int main(int argc, char** argv) {
  std::string auto_update_packages;
  auto cmdline = fxl::CommandLineFromArgcArgv(argc, argv);

  if (cmdline.HasOption(kAutoUpdatePackages)) {
    cmdline.GetOptionValue(kAutoUpdatePackages, &auto_update_packages);
  }

  zx_status_t status = InitStdinSocket();
  if (status != ZX_OK) {
    return status;
  }

  // Wire up standard streams. This sends all stdout and stderr to the debuglog.
  status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    return status;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto pa_directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  // NOTE: This is now load-bearing as of
  // https://fuchsia-review.googlesource.com/c/fuchsia/+/615184.
  // We needed a way to test that we were properly connecting to LogSink.
  FX_LOGS(INFO) << "Starting appmgr.";

  zx::channel svc_for_sys_server, svc_for_sys_client;
  status = zx::channel::create(0, &svc_for_sys_server, &svc_for_sys_client);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create channel: " << zx_status_get_string(status);
    return status;
  }
  status = fdio_open("/svc_for_sys",
                     static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                           fuchsia_io::wire::OpenFlags::kDirectory |
                                           fuchsia_io::wire::OpenFlags::kRightWritable),
                     svc_for_sys_server.release());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "failed to open /svc_for_sys (" << zx_status_get_string(status)
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
  root_realm_services->host_directory = environment_services->CloneChannel();

  zx::channel trace_client, trace_server;
  status = zx::channel::create(0, &trace_client, &trace_server);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create tracing channel: " << status;
    return status;
  }

  trace::TraceProvider trace_provider(std::move(trace_client), loop.dispatcher());

  auto lifecycle_request = zx_take_startup_handle(PA_LIFECYCLE);
  std::unordered_set<component::Moniker> lifecycle_allowlist;
  std::vector<std::string> sysmgr_args;
  if (!auto_update_packages.empty()) {
    sysmgr_args.push_back("--auto_update_packages=" + auto_update_packages);
  }
  std::string sysmgr_url;
  if (cmdline.HasOption(kLaunchSysmgr)) {
    std::string s;
    cmdline.GetOptionValue(kLaunchSysmgr, &s);
    if (s == "true") {
      sysmgr_url = "fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx";
    } else if (s == "false") {
      // default
    } else {
      FX_LOGS(ERROR) << "Invalid value for --launch_sysmgr: " << s;
      return ZX_ERR_INVALID_ARGS;
    }
  }
  component::AppmgrArgs args{.pa_directory_request = std::move(pa_directory_request),
                             .lifecycle_request = std::move(lifecycle_request),
                             .lifecycle_allowlist = std::move(lifecycle_allowlist),
                             .root_realm_services = std::move(root_realm_services),
                             .environment_services = std::move(environment_services),
                             .sysmgr_url = sysmgr_url,
                             .sysmgr_args = sysmgr_args,
                             .trace_server_channel = std::move(trace_server),
                             .stop_callback = [](zx_status_t status) { exit(status); }};
  component::Appmgr appmgr(loop.dispatcher(), std::move(args));

  loop.Run();
  return 0;
}
