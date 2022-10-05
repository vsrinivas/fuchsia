// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/perfetto-bridge/producer_connector_impl.h"

#include <fidl/fuchsia.tracing.perfetto/cpp/markers.h>
#include <fidl/fuchsia.tracing.perfetto/cpp/natural_types.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fit/include/lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <functional>
#include <memory>

#include <fbl/unique_fd.h>
#include <src/lib/fxl/memory/weak_ptr.h>

ProducerConnectorImpl::ProducerConnectorImpl(async_dispatcher_t* dispatcher,
                                             perfetto::base::TaskRunner* perfetto_task_runner,
                                             perfetto::ipc::Host* producer_host)
    : fidl_dispatcher_(dispatcher),
      perfetto_task_runner_(perfetto_task_runner),
      perfetto_producer_host_(producer_host) {
  FX_DCHECK(fidl_dispatcher_);
  FX_DCHECK(perfetto_task_runner_);
  FX_DCHECK(perfetto_producer_host_);
}

ProducerConnectorImpl::~ProducerConnectorImpl() = default;

void ProducerConnectorImpl::ConnectProducer(ConnectProducerRequest& request,
                                            ConnectProducerCompleter::Sync& completer) {
  // Validate that the client is providing the required resources.
  if (!request.producer_socket() || !request.buffer().from_server()) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Bind the incoming socket to a file descriptor.
  int sock_fd;
  zx_status_t status = fdio_fd_create(request.producer_socket().release(), &sock_fd);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to bind socket to FD: " << status;
    completer.Reply(fit::as_error(ZX_ERR_NO_RESOURCES));
    return;
  }
  perfetto::base::ScopedFile scoped_sock_fd(sock_fd);

  // Instantiate a FIDL client for asynchronously sending the shared memory buffer.
  // Each client is associated with an int-based ID so that its lifetime can be managed.
  // This could be done better with something like HLCPP's BindingSet<>.
  static ReceiverId next_buffer_receiver_id = 0;
  ReceiverId cur_client_id = next_buffer_receiver_id++;
  buffer_receivers_.emplace(
      cur_client_id, std::make_unique<BufferReceiverClient>(
                         *request.buffer().from_server().take(), fidl_dispatcher_,
                         [this, cur_client_id]() { OnBufferReceiverDisconnected(cur_client_id); }));

  // Create a callback used by Perfetto to send a shmem FD to the remote BufferReceiver.
  std::function send_fd_cb = [this, cur_client_id](int fd) {
    return SendSharedMemoryToProducer(cur_client_id, fd);
  };

  // Provide the socket and the FD-sending-callback to Perfetto.
  perfetto_task_runner_->PostTask(
      [host = this->perfetto_producer_host_, socket_fd = scoped_sock_fd.release(), send_fd_cb]() {
        host->AdoptConnectedSocket_Fuchsia(perfetto::base::ScopedFile(socket_fd), send_fd_cb);
      });

  completer.Reply(fit::ok());
}

bool ProducerConnectorImpl::SendSharedMemoryToProducer(ReceiverId receiver_id, int fd) {
  FX_CHECK(fd != perfetto::base::ScopedFile::kInvalid);

  if (buffer_receivers_.find(receiver_id) == buffer_receivers_.end()) {
    FX_LOGS(WARNING) << "Couldn't send Perfetto shmem buffer to disconnected client "
                     << receiver_id;
    return false;
  }

  fuchsia_tracing_perfetto::BufferReceiverProvideBufferRequest request;
  zx_status_t status;
  status = fdio_fd_clone(fd, request.buffer().channel().reset_and_get_address());
  FX_CHECK(status == ZX_OK) << "fdio_fd_clone " << status;

  (*buffer_receivers_[receiver_id]->client())
      ->ProvideBuffer(std::move(request))
      .Then([](fidl::Result<::fuchsia_tracing_perfetto::BufferReceiver::ProvideBuffer>& result) {
        if (result.is_error()) {
          FX_LOGS(ERROR) << "Error sending shared memory buffer to producer: "
                         << result.error_value().FormatDescription();
        }
      });

  return true;
}

void ProducerConnectorImpl::OnBufferReceiverDisconnected(ReceiverId receiver_id) {
  FX_DCHECK(buffer_receivers_.find(receiver_id) != buffer_receivers_.end());
  buffer_receivers_.erase(receiver_id);
}

ProducerConnectorImpl::BufferReceiverClient::BufferReceiverClient() = default;

ProducerConnectorImpl::BufferReceiverClient::BufferReceiverClient(
    fidl::ClientEnd<fuchsia_tracing_perfetto::BufferReceiver> client_end,
    async_dispatcher_t* dispatcher, std::function<void()> on_disconnect_cb)
    : client_(std::move(client_end), dispatcher, this),
      on_disconnect_cb_(std::move(on_disconnect_cb)) {}

ProducerConnectorImpl::BufferReceiverClient::~BufferReceiverClient() = default;

void ProducerConnectorImpl::BufferReceiverClient::on_fidl_error(fidl::UnbindInfo error) {
  // Signal to ProducerConnectorImpl that |this| BufferReceiver connection is closed.
  on_disconnect_cb_();
}
