// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/remote_api_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "lib/fpromise/bridge.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/async_util.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

#define FN(msg_type)                                                                             \
  void RemoteAPIImpl::msg_type(const debug_ipc::msg_type##Request& request,                      \
                               fit::callback<void(const Err&, debug_ipc::msg_type##Reply)> cb) { \
    Send(request, std::move(cb));                                                                \
  }

FOR_EACH_REQUEST_TYPE(FN)
#undef FN

template <typename SendMsgType, typename RecvMsgType>
void RemoteAPIImpl::Send(const SendMsgType& send_msg,
                         fit::callback<void(const Err&, RecvMsgType)> callback) {
  uint32_t transaction_id = session_->next_transaction_id_;
  session_->next_transaction_id_++;
  if (!session_->stream_) {
    // No connection, asynchronously issue the error.
    if (callback) {
      debug::MessageLoop::Current()->PostTask(FROM_HERE, [callback =
                                                              std::move(callback)]() mutable {
        callback(Err(ErrType::kNoConnection, "No connection to debugged system."), RecvMsgType());
      });
    }
    return;
  }
  session_->stream_->Write(debug_ipc::Serialize(send_msg, transaction_id));
  // This is the reply callback that unpacks the data in a vector, converts it to the requested
  // RecvMsgType struct, and issues the callback.
  Session::Callback dispatch_callback = [callback = std::move(callback)](
                                            const Err& err, std::vector<char> data) mutable {
    RecvMsgType reply;
    if (err.has_error()) {
      // Forward the error and ignore all data.
      if (callback)
        callback(err, std::move(reply));
      return;
    }
    uint32_t transaction_id = 0;
    Err deserialization_err;
    if (!debug_ipc::Deserialize(std::move(data), &reply, &transaction_id)) {
      reply = RecvMsgType();  // Could be in a half-read state.
      deserialization_err =
          Err(ErrType::kCorruptMessage,
              fxl::StringPrintf("Corrupt reply message for transaction %u.", transaction_id));
    }
    if (callback)
      callback(deserialization_err, std::move(reply));
  };
  session_->pending_.emplace(std::piecewise_construct, std::forward_as_tuple(transaction_id),
                             std::forward_as_tuple(std::move(dispatch_callback)));
}

}  // namespace zxdb
