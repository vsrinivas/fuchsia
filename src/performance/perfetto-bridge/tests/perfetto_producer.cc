// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Basic Component that produces perfetto tracing data
#include <fidl/fuchsia.tracing.perfetto/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/fd.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/observer.h>

#include <thread>

#include <perfetto/base/task_runner.h>
#include <perfetto/ext/ipc/client.h>
#include <perfetto/ext/tracing/core/producer.h>
#include <perfetto/ext/tracing/core/shared_memory.h>
#include <perfetto/ext/tracing/ipc/producer_ipc_client.h>
#include <perfetto/tracing/backend_type.h>
#include <perfetto/tracing/core/trace_config.h>
#include <perfetto/tracing/platform.h>
#include <perfetto/tracing/tracing.h>
#include <perfetto/tracing/track_event.h>

#include "lib/syslog/cpp/macros.h"

#include <protos/perfetto/trace/track_event/process_descriptor.gen.h>

class FuchsiaProducer : public perfetto::Producer {
 public:
  FuchsiaProducer() = default;
  void OnConnect() override { FX_LOGS(WARNING) << "Ignoring OnConnect"; }
  void OnDisconnect() override { FX_LOGS(WARNING) << "Ignoring OnDisconnect"; }
  void OnTracingSetup() override { FX_LOGS(WARNING) << "Ignoring OnTracingSetup"; }
  void OnStartupTracingSetup() override { FX_LOGS(WARNING) << "Ignoring StartupOnTracingSetup"; }
  void SetupDataSource(perfetto::DataSourceInstanceID, const perfetto::DataSourceConfig&) override {
    FX_LOGS(WARNING) << "Ignoring SetupDataSource";
  }
  void StartDataSource(perfetto::DataSourceInstanceID, const perfetto::DataSourceConfig&) override {
    FX_LOGS(WARNING) << "Ignoring StartDataSource";
  }
  void StopDataSource(perfetto::DataSourceInstanceID) override {
    FX_LOGS(WARNING) << "Ignoring StopDataSource";
  }
  void Flush(perfetto::FlushRequestID, const perfetto::DataSourceInstanceID* data_source_ids,
             size_t num_data_sources) override {
    FX_LOGS(WARNING) << "Ignoring Flush";
  }
  void ClearIncrementalState(const perfetto::DataSourceInstanceID* data_source_ids,
                             size_t num_data_sources) override {
    FX_LOGS(WARNING) << "Ignoring ClearIncrementalState";
  }
};

class PerfettoTraceProvider : public fidl::Server<fuchsia_tracing_perfetto::BufferReceiver> {
 public:
  static zx::result<> Serve(async_dispatcher_t* dispatcher) {
    // 1) Create sockets to communicate to the remote perfetto instance
    zx::socket local_perfetto_socket, remote_perfetto_socket;
    const zx_status_t status =
        zx::socket::create(0, &local_perfetto_socket, &remote_perfetto_socket);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    // 2) Implement buffer receiver and create a client end
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_tracing_perfetto::BufferReceiver>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    auto impl = std::make_unique<PerfettoTraceProvider>();
    auto platform = perfetto::Platform::GetDefaultPlatform();
    impl->task_runner_ = platform->CreateTaskRunner({.name_for_debugging = "TestPerfetto"});

    int fd;
    const zx_status_t fd_status = fdio_fd_create(local_perfetto_socket.release(), &fd);
    impl->fd_ = fd;
    if (fd_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create an fd for perfetto: " << fd_status;
      return zx::error(status);
    }

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kSystemBackend;
    perfetto::Tracing::Initialize(args);

    perfetto::base::ScopedSocketHandle scoped_handle(fd);
    perfetto::ipc::Client::ConnArgs conn_args(std::move(scoped_handle));
    conn_args.receive_shmem_fd_cb_fuchsia = [fd]() { return fd; };
    impl->producer_ = std::make_unique<FuchsiaProducer>();
    impl->perfetto_service_ = perfetto::ProducerIPCClient::Connect(
        std::move(conn_args), impl->producer_.get(), "perfetto_producer", impl->task_runner_.get(),
        perfetto::TracingService::ProducerSMBScrapingMode::kEnabled, 4000, 4000);

    const fidl::ServerBindingRef binding_ref =
        fidl::BindServer(dispatcher, std::move(std::move(endpoints->server)), std::move(impl));
    auto trace_buffer_receiver =
        fuchsia_tracing_perfetto::TraceBuffer::WithFromServer(std::move(endpoints->client));

    // 3) Connect from Component to perfetto-bridge via ProducerConnector
    zx::result client_end = component::Connect<fuchsia_tracing_perfetto::ProducerConnector>();
    if (client_end.is_error()) {
      FX_LOGS(ERROR) << "Failed to connect to Producer Connector: " << client_end.status_string();
      return client_end.take_error();
    }
    const fidl::SyncClient client{std::move(*client_end)};

    // 4) Send one socket and the buffer receiver client end to perfetto bridge using
    // ConnectProducer
    auto result = client->ConnectProducer(
        {std::move(remote_perfetto_socket), std::move(trace_buffer_receiver)});
    if (result.is_error()) {
      FX_LOGS(ERROR) << "ConnectProducer failed! " << result.error_value();
      return zx::error(ZX_ERR_NOT_CONNECTED);
    }

    // 5) Perfetto bridge sends a buffer to the component using BufferReceiver
    // 6) Initialize perfetto using the other socket and the received buffer
    return zx::ok();
  }

  void ProvideBuffer(ProvideBufferRequest& request,
                     ProvideBufferCompleter::Sync& completer) override {
    zx::channel file_handle = request.buffer().TakeChannel();
    completer.Reply({fit::success<>()});
  }

 private:
  std::unique_ptr<perfetto::TracingService::ProducerEndpoint> perfetto_service_;
  std::unique_ptr<perfetto::base::TaskRunner> task_runner_;
  std::unique_ptr<FuchsiaProducer> producer_;
  int fd_;
};

// Set up of test events to check for
PERFETTO_DEFINE_CATEGORIES(perfetto::Category("test").SetDescription("A Test Event"));
PERFETTO_TRACK_EVENT_STATIC_STORAGE();
void EmitEvent(int count) { TRACE_EVENT("test", "SomeEvent", "count", count); }
void EmitEvents() {
  TRACE_EVENT_BEGIN("test", "Event1");
  EmitEvent(1);
  EmitEvent(2);
  TRACE_EVENT_END("test");
  TRACE_COUNTER("test", "Counter1", 120);
}

std::unique_ptr<perfetto::TracingSession> session;
void OnTraceStateUpdate() {
  switch (trace_state()) {
    case TRACE_STARTED:
      EmitEvents();
      break;
    case TRACE_STOPPING:
    case TRACE_STOPPED:
      break;
  }
}

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "perfetto_producer");

  trace::TraceObserver trace_observer;
  trace_observer.Start(dispatcher, OnTraceStateUpdate);
  if (PerfettoTraceProvider::Serve(dispatcher).is_error()) {
    FX_LOGS(ERROR) << "Failed to start PerfettoTraceProvider!";
    return 1;
  }

  loop.Run();
}
