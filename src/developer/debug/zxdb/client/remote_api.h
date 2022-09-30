// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_H_

#include "lib/fit/function.h"
#include "lib/syslog/cpp/macros.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

class Err;

// Abstracts the IPC layer for sending messages to the debug agent. This allows mocking of the
// interface without dealing with the innards of the serialization.
//
// The default implementations of each of these functions asserts. The Session implements overrides
// that actually send and receive messages. Tests should derive from this and implement the messages
// they expect.
class RemoteAPI {
 public:
  virtual ~RemoteAPI() = default;

#define FN(msg_type)                                                                      \
  virtual void msg_type(const debug_ipc::msg_type##Request& request,                      \
                        fit::callback<void(const Err&, debug_ipc::msg_type##Reply)> cb) { \
    FX_NOTIMPLEMENTED();                                                                  \
  }

  FOR_EACH_REQUEST_TYPE(FN)
#undef FN
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_H_
