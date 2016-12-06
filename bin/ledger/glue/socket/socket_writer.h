// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_WRITER_H_
#define APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_WRITER_H_

#include <memory>
#include <string>

#include "lib/fidl/c/waiter/async_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/macros.h"
#include "mx/socket.h"

namespace glue {

// Deletes itself when the socket is closed or the write is completed.
class SocketWriter {
 public:
  SocketWriter(const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter());
  ~SocketWriter();

  void Start(std::string data, mx::socket destination);

 private:
  void WriteData();
  void WaitForSocket();
  static void WaitComplete(mx_status_t result,
                           mx_signals_t pending,
                           void* context);
  void Done();

  std::string data_;
  // Position of the next byte in |data_| to be written.
  size_t offset_ = 0u;
  mx::socket destination_;
  const FidlAsyncWaiter* waiter_;
  FidlAsyncWaitID wait_id_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(SocketWriter);
};

}  // namespace glue

#endif  // APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_WRITER_H_
