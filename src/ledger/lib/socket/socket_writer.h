// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_SOCKET_SOCKET_WRITER_H_
#define SRC_LEDGER_LIB_SOCKET_SOCKET_WRITER_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>

#include <functional>
#include <memory>
#include <string>

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace socket {

// Requests data to write from its client. Interrupts itself and closes the
// socket when deleted.
class SocketWriter {
 public:
  class Client {
   public:
    virtual void GetNext(size_t offset, size_t max_size,
                         fit::function<void(absl::string_view)> callback) = 0;
    virtual void OnDataComplete() = 0;

   protected:
    virtual ~Client() {}
  };

  explicit SocketWriter(Client* client,
                        async_dispatcher_t* dispatcher = async_get_default_dispatcher());
  SocketWriter(const SocketWriter&) = delete;
  SocketWriter& operator=(const SocketWriter&) = delete;
  ~SocketWriter();

  void Start(zx::socket destination);

 private:
  void GetData();
  void WriteData(absl::string_view data);
  void Done();

  Client* const client_;
  async_dispatcher_t* const dispatcher_;
  // Position of the next byte to request.
  size_t offset_ = 0u;
  // Data left to send from last call to |GetNext|.
  std::string data_;
  // Data left to send.
  absl::string_view data_view_;
  zx::socket destination_;
  async::Wait wait_;
};

// Writes the content of a string to a socket. Deletes itself when done.
class StringSocketWriter : public SocketWriter::Client {
 public:
  explicit StringSocketWriter(async_dispatcher_t* dispatcher = async_get_default_dispatcher());

  void Start(std::string data, zx::socket destination);

 private:
  void GetNext(size_t offset, size_t max_size,
               fit::function<void(absl::string_view)> callback) override;
  void OnDataComplete() override;

  SocketWriter socket_writer_;
  std::string data_;
};

}  // namespace socket

#endif  // SRC_LEDGER_LIB_SOCKET_SOCKET_WRITER_H_
