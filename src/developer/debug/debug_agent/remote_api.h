// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_REMOTE_API_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_REMOTE_API_H_

#include "src/developer/debug/ipc/protocol.h"

namespace debug_agent {

// This is an abstract class that implements calls corresponding to the
// client->agent IPC requests.
class RemoteAPI {
 public:
#define FN(type) \
  virtual void On##type(const debug_ipc::type##Request& request, debug_ipc::type##Reply* reply) = 0;

  FOR_EACH_REQUEST_TYPE(FN)
#undef FN
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_REMOTE_API_H_
