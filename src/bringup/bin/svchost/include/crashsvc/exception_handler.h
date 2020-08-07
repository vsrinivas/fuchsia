// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_SVCHOST_INCLUDE_CRASHSVC_EXCEPTION_HANDLER_H_
#define SRC_BRINGUP_BIN_SVCHOST_INCLUDE_CRASHSVC_EXCEPTION_HANDLER_H_

#include <fuchsia/exception/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/handle.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

class ExceptionHandler {
 public:
  ExceptionHandler(async_dispatcher_t* dispatcher, zx_handle_t exception_handler_svc);

  void Handle(zx::exception exception, const zx_exception_info_t& info);

  // Exposed for testing.
  bool ConnectedToServer() const;

 private:
  void SetUpClient(zx::channel client_endpoint = zx::channel{});

  // Send |server_endpoit_| to the server of fuchsia.exception.Handler.
  void ConnectToServer();

  void OnUnbind(fidl::UnbindInfo info, zx::channel channel);

  async_dispatcher_t* dispatcher_;
  zx_handle_t exception_handler_svc_;

  // Becomes true if exceptions cannot be sent to the exception handling service. Once we reach that
  // state, we will never handle exceptions again.
  bool drop_exceptions_;
  fidl::Client<llcpp::fuchsia::exception::Handler> connection_;

  // The other endpoint of the |connection_|'s channel before it has been sent to the server.
  zx::channel server_endpoint_;
};

#endif  // SRC_BRINGUP_BIN_SVCHOST_INCLUDE_CRASHSVC_EXCEPTION_HANDLER_H_
