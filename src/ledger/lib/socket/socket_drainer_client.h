// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_SOCKET_SOCKET_DRAINER_CLIENT_H_
#define SRC_LEDGER_LIB_SOCKET_SOCKET_DRAINER_CLIENT_H_

#include <lib/fit/function.h>

#include <functional>
#include <memory>
#include <string>

#include "src/lib/callback/destruction_sentinel.h"
#include "src/lib/fsl/socket/socket_drainer.h"

namespace socket {

class SocketDrainerClient : public fsl::SocketDrainer::Client {
 public:
  SocketDrainerClient();
  SocketDrainerClient(const SocketDrainerClient&) = delete;
  SocketDrainerClient& operator=(const SocketDrainerClient&) = delete;
  ~SocketDrainerClient() override;

  void Start(zx::socket source, fit::function<void(std::string)> callback);

  void SetOnDiscardable(fit::closure on_discardable);

  bool IsDiscardable() const;

 private:
  void OnDataAvailable(const void* data, size_t num_bytes) override;

  void OnDataComplete() override;

  fit::function<void(std::string)> callback_;
  std::string data_;
  fsl::SocketDrainer drainer_;
  fit::closure on_discardable_;
  bool discardable_ = false;
  callback::DestructionSentinel destruction_sentinel_;
};

}  // namespace socket

#endif  // SRC_LEDGER_LIB_SOCKET_SOCKET_DRAINER_CLIENT_H_
