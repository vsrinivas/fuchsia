// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_IMPL_H_

#include <cstdint>

#include "src/developer/debug/zxdb/client/remote_api.h"

namespace zxdb {

class Session;

// An implementation of RemoteAPI for Session. This class is logically part of the Session class
// (it's a friend) but is separated out for clarity.
class RemoteAPIImpl : public RemoteAPI {
 public:
  // The session must outlive this object.
  explicit RemoteAPIImpl(Session* session) : session_(session) {}

  // RemoteAPI implementation.
  void SetVersion(uint32_t version) override { version_ = version; }

#define FN(msg_type)                                                 \
  virtual void msg_type(const debug_ipc::msg_type##Request& request, \
                        fit::callback<void(const Err&, debug_ipc::msg_type##Reply)> cb) override;

  FOR_EACH_REQUEST_TYPE(FN)
#undef FN

 private:
  // Sends a message with an asynchronous reply.
  //
  // The callback will be issued with an Err struct. If the Err object indicates an error, the
  // request has failed and the reply data will not be set (it will contain the default-constructed
  // data).
  //
  // The callback will always be issued asynchronously (not from withing the Send() function
  // itself).
  template <typename SendMsgType, typename RecvMsgType>
  void Send(const SendMsgType& send_msg, fit::callback<void(const Err&, RecvMsgType)> callback);

  Session* session_;

  uint32_t version_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteAPIImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_IMPL_H_
