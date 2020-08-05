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
      is_bound_(false),
      handler_() {
  SetUpClient();
}

void ExceptionHandler::SetUpClient(zx::channel client) {
  // We are in a build without a server for fuchsia.exception.Handler, e.g., bringup.
  if (exception_handler_svc_ == ZX_HANDLE_INVALID) {
    is_bound_ = false;
    return;
  }

  zx::channel server;
  if (const zx_status_t status = zx::channel::create(0u, &server, &client)) {
    LogError("Failed to create channel", status);
    is_bound_ = false;
    return;
  }

  if (const zx_status_t status = fdio_service_connect_at(
          exception_handler_svc_, llcpp::fuchsia::exception::Handler::Name, server.release());
      status != ZX_OK) {
    LogError("Unable to connect to fuchsia.exception.Handler", status);
    is_bound_ = false;
    return;
  }

  handler_ = fidl::Client<llcpp::fuchsia::exception::Handler>();
  handler_.Bind(std::move(client), dispatcher_, [this](fidl::UnbindInfo info, zx::channel channel) {
    OnUnbind(info, std::move(channel));
  });

  is_bound_ = true;
}

void ExceptionHandler::OnUnbind(const fidl::UnbindInfo info, zx::channel channel) {
  // If the unbind was not an error, don't reconnect and stop sending exceptions to
  // fuchsia.exception.Handler. This should only happen in tests.
  if (info.status == ZX_OK || info.status == ZX_ERR_CANCELED) {
    is_bound_ = false;
    return;
  }

  LogError("Lost connection to fuchsia.exception.Handler", info.status);

  // Immediately attempt to reconnect to fuchsia.exception.Handler. An exponential backoff is not
  // used because a reconnection loop will only ever happen if the build does not contain a server
  // for the protocol and crashsvc is configured to use the exception handling server.
  //
  // TODO(56491): figure out a way to detect if the process holding the other end of |channel| has
  // crashed and stop sending exceptions to it.
  SetUpClient(std::move(channel));
}

void ExceptionHandler::Handle(zx::exception exception, const zx_exception_info_t& info) {
  if (!is_bound_) {
    return;
  }

  llcpp::fuchsia::exception::ExceptionInfo exception_info;
  exception_info.process_koid = info.pid;
  exception_info.thread_koid = info.tid;
  exception_info.type = static_cast<llcpp::fuchsia::exception::ExceptionType>(info.type);

  if (const auto result = handler_->OnException(std::move(exception), exception_info, [] {});
      result.status() != ZX_OK) {
    LogError("Failed to pass exception to handler", info, result.status());
  }
}
