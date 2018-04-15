// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/system_impl.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/lib/debug_ipc/client_protocol.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

class ProcessImpl;
class ThreadImpl;

class Session {
 public:
  // The stream buffer must outlive this class.
  explicit Session(debug_ipc::StreamBuffer* stream);
  ~Session();

  // Notification that data is available to be read on the StreamBuffer.
  void OnStreamReadable();

  // Access to the singleton corresponding to the debugged system.
  System& system() { return system_; }

  // Sends a message with an asynchronous reply. Returns the transaction_id.
  //
  // The callback will be issued with the transaction_id (for tracking) and
  // an Err struct. If the Err object indicates an error, the request has
  // failed and the reply data will not be set (it will contain the
  // default-constructed data).
  template <typename SendMsgType, typename RecvMsgType>
  uint32_t Send(const SendMsgType& send_msg,
                std::function<void(const Err&, RecvMsgType)> callback);

 private:
  // Nonspecific callback type. Implemented by SessionDispatchCallback (with
  // the type-specific parameter pre-bound). The uint32_t is the transaction
  // ID. If the error is set, the data will be invalid and the callback should
  // be issued with the error instead of trying to deserialize.
  using Callback =
      std::function<void(const Err&, std::vector<char>)>;

  // Dispatches unsolicited notifications sent from the agent.
  void DispatchNotification(const debug_ipc::MsgHeader& header,
                            std::vector<char> data);

  // Returns the thread object from the given koids, or null.
  ThreadImpl* ThreadImplFromKoid(uint64_t process_koid, uint64_t thread_koid);

  debug_ipc::StreamBuffer* stream_;  // Non-owning.

  std::map<uint32_t, Callback> pending_;
  uint32_t next_transaction_id_ = 1;  // Reserve 0 for notifications.

  SystemImpl system_;
};

template <typename SendMsgType, typename RecvMsgType>
uint32_t Session::Send(
    const SendMsgType& send_msg,
    std::function<void(const Err&, RecvMsgType)> callback) {
  uint32_t transaction_id = next_transaction_id_;
  next_transaction_id_++;

  debug_ipc::MessageWriter writer(sizeof(SendMsgType));
  debug_ipc::WriteRequest(send_msg, transaction_id, &writer);

  std::vector<char> serialized = writer.MessageComplete();
  stream_->Write(std::move(serialized));

  // This is the reply callback that unpacks the data in a vector, converts it
  // to the requested RecvMsgType struct, and issues the callback.
  Callback dispatch_callback = [callback = std::move(callback)](
      const Err& err, std::vector<char> data) {
    RecvMsgType reply;
    if (err.has_error()) {
      // Forward the error and ignore all data.
      if (callback)
        callback(err, std::move(reply));
      return;
    }

    debug_ipc::MessageReader reader(std::move(data));

    uint32_t transaction_id = 0;
    Err deserialization_err;
    if (!debug_ipc::ReadReply(&reader, &reply, &transaction_id)) {
      reply = RecvMsgType();  // Could be in a half-read state.
      deserialization_err =
          Err(ErrType::kCorruptMessage, fxl::StringPrintf(
                "Corrupt reply message for transaction %u.", transaction_id));
    }

    if (callback)
      callback(deserialization_err, std::move(reply));
  };

  pending_.emplace(std::piecewise_construct,
                   std::forward_as_tuple(transaction_id),
                   std::forward_as_tuple(std::move(dispatch_callback)));
  return transaction_id;
}

}  // namespace zxdb
