// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/status.h>

#include <crashsvc/exception_handler.h>
#include <crashsvc/logging.h>

ExceptionHandler::ExceptionHandler(async_dispatcher_t* dispatcher,
                                   zx_handle_t exception_handler_svc)
    : dispatcher_(dispatcher),
      exception_handler_svc_(exception_handler_svc),
      // We are in a build without a server for fuchsia.exception.Handler, e.g., bringup.
      drop_exceptions_(exception_handler_svc_ == ZX_HANDLE_INVALID),
      connection_() {
  SetUpClient();
  ConnectToServer();
}

void ExceptionHandler::SetUpClient() {
  if (drop_exceptions_) {
    return;
  }

  auto exception_handler_endpoints = fidl::CreateEndpoints<fuchsia_exception::Handler>();
  if (!exception_handler_endpoints.is_ok()) {
    LogError("Failed to create channel for fuchsia.exception.Handler",
             exception_handler_endpoints.status_value());
    drop_exceptions_ = true;
    return;
  }

  connection_ = {};
  connection_.Bind(std::move(exception_handler_endpoints->client), dispatcher_, this);
  server_endpoint_ = std::move(exception_handler_endpoints->server);
}

void ExceptionHandler::on_fidl_error(const fidl::UnbindInfo info) {
  // If the unbind was only due to dispatcher shutdown, don't reconnect and stop sending exceptions
  // to fuchsia.exception.Handler. This should only happen in tests.
  if (info.reason() == fidl::Reason::kDispatcherError && info.status() == ZX_ERR_CANCELED) {
    drop_exceptions_ = true;
    return;
  }

  LogError("Lost connection to fuchsia.exception.Handler", info.status());

  // We immediately bind the |connection_| again, but we don't re-connect to the server of
  // fuchsia.exception.Handler, i.e sending the other endpoint of the channel to the server. Instead
  // the re-connection will be done on the next exception. The reason we don't re-connect (1)
  // immediately is because the server could have been shut down by the system or (2) with a backoff
  // is because we don't want to be queueing up exceptions which underlying processes need to be
  // terminated.
  SetUpClient();
}

void ExceptionHandler::ConnectToServer() {
  if (ConnectedToServer() || drop_exceptions_) {
    return;
  }

  if (const zx_status_t status = fdio_service_connect_at(
          exception_handler_svc_, fidl::DiscoverableProtocolName<fuchsia_exception::Handler>,
          server_endpoint_.channel().release());
      status != ZX_OK) {
    LogError("unable to connect to fuchsia.exception.Handler", status);
    drop_exceptions_ = true;
    return;
  }
}

void ExceptionHandler::Handle(zx::exception exception, const zx_exception_info_t& info) {
  if (drop_exceptions_) {
    return;
  }

  ConnectToServer();

  fuchsia_exception::wire::ExceptionInfo exception_info;
  exception_info.process_koid = info.pid;
  exception_info.thread_koid = info.tid;
  exception_info.type = static_cast<fuchsia_exception::wire::ExceptionType>(info.type);

  connection_->OnException(
      std::move(exception), exception_info,
      [info](fidl::WireUnownedResult<fuchsia_exception::Handler::OnException>&& result) {
        if (!result.ok()) {
          LogError("Failed to pass exception to handler", info, result.status());
        }
      });
}

bool ExceptionHandler::ConnectedToServer() const { return !server_endpoint_.is_valid(); }
