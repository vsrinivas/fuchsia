// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_COMMAND_H_
#define APPS_MOTERM_COMMAND_H_

#include <fdio/util.h>
#include <launchpad/launchpad.h>
#include <zx/process.h>
#include <zx/socket.h>

#include <functional>
#include <vector>

#include <async/auto_wait.h>
#include "lib/fsl/io/redirection.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/closure.h"

namespace moterm {

class Command {
 public:
  using ReceiveCallback =
      std::function<void(const void* bytes, size_t num_bytes)>;

  Command();

  bool Start(std::vector<std::string> command,
             std::vector<fsl::StartupHandle> startup_handles,
             ReceiveCallback receive_callback,
             fxl::Closure termination_callback);

  void SendData(const void* bytes, size_t num_bytes);

 private:
  async_wait_result_t OnProcessTerminated(zx_handle_t process_handle,
                                          async_t*,
                                          zx_status_t status,
                                          const zx_packet_signal* signal);
  // |socket| might be either |stdout_| or |stderr_|.
  async_wait_result_t OnSocketReadable(zx::socket* socket,
                                       async_t*,
                                       zx_status_t status,
                                       const zx_packet_signal* signal);

  fxl::Closure termination_callback_;
  ReceiveCallback receive_callback_;
  zx::socket stdin_;
  zx::socket stdout_;
  zx::socket stderr_;

  std::unique_ptr<async::AutoWait> termination_waiter_ = 0;
  std::unique_ptr<async::AutoWait> stdout_waiter_ = 0;
  std::unique_ptr<async::AutoWait> stderr_waiter_ = 0;
  zx::process process_;
};

}  // namespace moterm

#endif  // APPS_MOTERM_COMMAND_H_
