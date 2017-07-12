// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_WRITER_H_
#define APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_WRITER_H_

#include <functional>
#include <memory>
#include <string>

#include "lib/fidl/c/waiter/async_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"
#include "mx/socket.h"

namespace glue {

// Requests data to write from its client. Interrupts itself and closes the
// socket when deleted.
class SocketWriter {
 public:
  class Client {
   public:
    virtual void GetNext(size_t offset,
                         size_t max_size,
                         std::function<void(ftl::StringView)> callback) = 0;
    virtual void OnDataComplete() = 0;

   protected:
    virtual ~Client() {}
  };

  SocketWriter(Client* client,
               const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter());
  ~SocketWriter();

  void Start(mx::socket destination);

 private:
  void GetData();
  void WriteData(ftl::StringView data);
  void WaitForSocket();
  static void WaitComplete(mx_status_t result,
                           mx_signals_t pending,
                           uint64_t count,
                           void* context);
  void Done();

  Client* client_;
  // Position of the next byte to request.
  size_t offset_ = 0u;
  // Data left to send from last call to |GetNext|.
  std::string data_;
  // Data left to send.
  ftl::StringView data_view_;
  mx::socket destination_;
  const FidlAsyncWaiter* waiter_;
  FidlAsyncWaitID wait_id_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(SocketWriter);
};

// Writes the content of a string to a socket. Deletes itself when done.
class StringSocketWriter : public SocketWriter::Client {
 public:
  StringSocketWriter(
      const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter());

  void Start(std::string data, mx::socket destination);

 private:
  void GetNext(size_t offset,
               size_t max_size,
               std::function<void(ftl::StringView)> callback) override;
  void OnDataComplete() override;

  SocketWriter socket_writer_;
  std::string data_;
};

}  // namespace glue

#endif  // APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_WRITER_H_
