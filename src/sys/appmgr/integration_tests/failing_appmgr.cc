// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This component runs a hermetic instance of appmgr that points to a nonexistent sysmgr, to
// simulate sysmgr terminating while appmgr is running.
//
// In response, we expect appmgr to trigger a reboot.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>

#include <memory>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/sys/appmgr/appmgr.h"

class AppmgrHarness : public fuchsia::sys::internal::LogConnectionListener {
 public:
  void Run() {
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

    FX_LOGS(INFO) << "Started failing_appmgr";
    zx::channel env_request_server, env_request_client;
    zx_status_t status = zx::channel::create(0, &env_request_server, &env_request_client);
    FX_CHECK(status == ZX_OK) << status;
    auto environment_services =
        std::make_shared<sys::ServiceDirectory>(std::move(env_request_client));

    zx::channel trace_client, trace_server;
    status = zx::channel::create(0, &trace_client, &trace_server);
    FX_CHECK(status == ZX_OK) << status;

    zx::channel appmgr_service_request;
    auto appmgr_services = sys::ServiceDirectory::CreateWithRequest(&appmgr_service_request);

    fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);
    component::AppmgrArgs args{
        .pa_directory_request = appmgr_service_request.release(),
        .root_realm_services = std::move(root_realm_services),
        .environment_services = std::move(environment_services),
        .sysmgr_url =
            "fuchsia-pkg://fuchsia.com/appmgr_integration_tests#meta/nonexistent_sysmgr.cmx",
        .sysmgr_args = {},
        .run_virtual_console = false,
        .trace_server_channel = std::move(trace_server)};
    component::Appmgr appmgr(loop.dispatcher(), std::move(args));

    fuchsia::sys::internal::LogConnectorPtr log_connector;
    log_connector.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "Failed to connect to appmgr logs: " << status;
    });

    status = appmgr_services->Connect(log_connector.NewRequest(),
                                      "appmgr_svc/fuchsia.sys.internal.LogConnector");
    FX_CHECK(status == ZX_OK);
    // Simulate the archivist connecting to the log listener so that appmgr will launch sysmgr
    log_connector->TakeLogConnectionListener(log_binding_.GetHandler(this));

    loop.Run();
  }

  // |fuchsia::sys::internal::LogConnectionListener|
  void OnNewConnection(fuchsia::sys::internal::LogConnection connection) override {}

 private:
  fidl::BindingSet<fuchsia::sys::internal::LogConnectionListener> log_binding_;
};

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  AppmgrHarness harness;
  harness.Run();
  FX_LOGS(FATAL) << "Loop quit unexpectedly";

  return 0;
}
