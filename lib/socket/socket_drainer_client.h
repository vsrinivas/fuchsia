// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_SOCKET_SOCKET_DRAINER_CLIENT_H_
#define PERIDOT_LIB_SOCKET_SOCKET_DRAINER_CLIENT_H_

#include <functional>
#include <memory>
#include <string>

#include <lib/fit/function.h>

#include "lib/callback/destruction_sentinel.h"
#include "lib/fsl/socket/socket_drainer.h"
#include "lib/fxl/macros.h"

namespace socket {

class SocketDrainerClient : public fsl::SocketDrainer::Client {
 public:
  SocketDrainerClient();

  ~SocketDrainerClient() override;

  void Start(zx::socket source, fit::function<void(std::string)> callback);

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

 private:
  void OnDataAvailable(const void* data, size_t num_bytes) override;

  void OnDataComplete() override;

  fit::function<void(std::string)> callback_;
  std::string data_;
  fsl::SocketDrainer drainer_;
  fit::closure on_empty_callback_;
  callback::DestructionSentinel destruction_sentinel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketDrainerClient);
};

}  // namespace socket

#endif  // PERIDOT_LIB_SOCKET_SOCKET_DRAINER_CLIENT_H_
