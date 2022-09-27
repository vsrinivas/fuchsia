// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.tracing.perfetto/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <perfetto/ext/base/thread_task_runner.h>
#include <perfetto/ext/tracing/ipc/service_ipc_host.h>
#include <perfetto/tracing/platform.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>

#include "src/performance/perfetto-bridge/consumer_adapter.h"
#include "src/performance/perfetto-bridge/producer_connector_impl.h"

int main(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl)) {
    return EXIT_FAILURE;
  }

  // Set up the FIDL task runner.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Set up the Perfetto environment and task runner.
  auto platform = perfetto::Platform::GetDefaultPlatform();
  auto perfetto_task_runner = platform->CreateTaskRunner({.name_for_debugging = "Perfetto"});
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "perfetto-bridge");

  // Start up the Perfetto service and IPC host.
  perfetto::ipc::Host* producer_host_ptr = nullptr;
  std::unique_ptr<perfetto::ServiceIPCHost> ipc_host =
      perfetto::ServiceIPCHost::CreateInstance(perfetto_task_runner.get());
  std::condition_variable cv;
  perfetto_task_runner->PostTask(
      [&cv, &producer_host_ptr, &ipc_host, perfetto_task_runner = perfetto_task_runner.get()]() {
        auto producer_host = perfetto::ipc::Host::CreateInstance_Fuchsia(perfetto_task_runner);
        producer_host_ptr = producer_host.get();

        auto consumer_host = perfetto::ipc::Host::CreateInstance_Fuchsia(perfetto_task_runner);
        FX_CHECK(ipc_host->Start(std::move(producer_host), std::move(consumer_host)))
            << "Perfetto IPC host failed to start.";
        cv.notify_one();
      });
  std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock);

  // Create a single instance of ProducerConnectorImpl, to be shared across all clients.
  ProducerConnectorImpl producer_connector_service(loop.dispatcher(), perfetto_task_runner.get(),
                                                   producer_host_ptr);

  // Instantiate an in-process consumer client.
  ConsumerAdapter consumer(ipc_host->service(), perfetto_task_runner.get());

  // Expose the FIDL server.
  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(dispatcher);
  zx::status result = outgoing.AddProtocol<fuchsia_tracing_perfetto::ProducerConnector>(
      [dispatcher, service = &producer_connector_service](
          fidl::ServerEnd<fuchsia_tracing_perfetto::ProducerConnector> server_end) {
        fidl::BindServer(dispatcher, std::move(server_end), service,
                         [](ProducerConnectorImpl*, fidl::UnbindInfo,
                            fidl::ServerEnd<fuchsia_tracing_perfetto::ProducerConnector>) {
                           // No per-connection state is maintained; no clean up is required.
                         });
      });
  FX_CHECK(result.is_ok()) << "Failed to expose ProducerConnector protocol: "
                           << result.status_string();

  result = outgoing.ServeFromStartupInfo();
  FX_CHECK(result.is_ok()) << "Failed to serve outgoing directory: " << result.status_string();

  return loop.Run() == ZX_OK;
}
