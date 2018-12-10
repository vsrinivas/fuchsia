// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/integration_tests/mock_stream_backend.h"

#include "garnet/lib/debug_ipc/message_reader.h"
#include "lib/component/cpp/environment_services_helper.h"

namespace debug_agent {

MockStreamBackend::MockStreamBackend() {
  // We initialize the stream and pass it on to the debug agent, which will
  // think it's correctly connected to a client.
  stream_.set_writer(this);
  auto environment_services = component::GetEnvironmentServices();
  agent_ = std::make_unique<DebugAgent>(&stream_,
                                        std::move(environment_services));
}

size_t MockStreamBackend::ConsumeStreamBufferData(const char* data,
                                                  size_t len) {
  // We assume we always get a header.
  const debug_ipc::MsgHeader* header =
      reinterpret_cast<const debug_ipc::MsgHeader*>(data);

  // Buffer for the message.
  std::vector<char> msg_buffer;
  msg_buffer.reserve(len);
  msg_buffer.insert(msg_buffer.end(), data, data + len);

  // Dispatch the messages we find interesting.
  debug_ipc::MessageReader reader(std::move(msg_buffer));
  switch (header->type) {
    case debug_ipc::MsgHeader::Type::kNotifyModules:
      HandleNotifyModules(&reader);
      break;
    case debug_ipc::MsgHeader::Type::kNotifyException:
      HandleNotifyException(&reader);
      break;
    default:
      // NOTE: Here is where you add more notification handlers as they are
      //       sent by the debug agent.
      break;
  }

  // Say we read the whole message.
  return len;
}

}  // namespace debug_agent
