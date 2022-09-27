// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_PERFETTO_BRIDGE_PRODUCER_CONNECTOR_IMPL_H_
#define SRC_PERFORMANCE_PERFETTO_BRIDGE_PRODUCER_CONNECTOR_IMPL_H_

#include <fidl/fuchsia.tracing.perfetto/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>

#include <perfetto/base/task_runner.h>
#include <perfetto/ext/ipc/host.h>
#include <perfetto/tracing.h>

namespace perfetto {
class ServiceIPCHost;
}  // namespace perfetto

// Implementation of the ProducerConnector server, which connects sockets for stream-based IPC
// transport and mediates the exchange shared memory buffers between Perfetto peers.
class ProducerConnectorImpl : public fidl::Server<fuchsia_tracing_perfetto::ProducerConnector> {
 public:
  ProducerConnectorImpl(async_dispatcher_t* dispatcher,
                        perfetto::base::TaskRunner* perfetto_task_runner,
                        perfetto::ipc::Host* producer_host);
  ~ProducerConnectorImpl() override;

  ProducerConnectorImpl(const ProducerConnectorImpl&) = delete;
  void operator=(const ProducerConnectorImpl&) = delete;

  // fuchsia::tracing::perfetto::ProducerConnector implementation.
  void ConnectProducer(ConnectProducerRequest& request,
                       ConnectProducerCompleter::Sync& completer) final;

 private:
  using ReceiverId = int;

  // Holds a client connection to a BufferReceiver service and invokes a callback on client
  // disconnect.
  class BufferReceiverClient
      : public fidl::AsyncEventHandler<fuchsia_tracing_perfetto::BufferReceiver> {
   public:
    BufferReceiverClient();
    BufferReceiverClient(fidl::ClientEnd<fuchsia_tracing_perfetto::BufferReceiver> client_end,
                         async_dispatcher_t* dispatcher, std::function<void()> on_disconnect_cb);
    ~BufferReceiverClient() override;

    fidl::Client<fuchsia_tracing_perfetto::BufferReceiver>* client() { return &client_; }

    // fidl::AsyncEventHandler implementation.
    // Invokes |on_disconnect_cb_| when |client_| disconnected.
    void on_fidl_error(fidl::UnbindInfo error) override;

   private:
    fidl::Client<fuchsia_tracing_perfetto::BufferReceiver> client_;
    std::function<void()> on_disconnect_cb_;
  };

  bool SendSharedMemoryToProducer(ReceiverId receiver_id, int fd);
  void OnBufferReceiverDisconnected(ReceiverId receiver_id);

  async_dispatcher_t* fidl_dispatcher_;

  // |perfetto_producer_host_| must be accessed on |perfetto_task_runer_|.
  perfetto::base::TaskRunner* perfetto_task_runner_;
  perfetto::ipc::Host* perfetto_producer_host_;

  std::map<ReceiverId, std::unique_ptr<BufferReceiverClient>> buffer_receivers_;
};

#endif  // SRC_PERFORMANCE_PERFETTO_BRIDGE_PRODUCER_CONNECTOR_IMPL_H_
