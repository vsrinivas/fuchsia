// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/integration_tests/mock_stream_backend.h"

#include "lib/fxl/logging.h"
#include "lib/sys/cpp/service_directory.h"
#include "src/developer/debug/ipc/message_reader.h"

namespace debug_agent {

MockStreamBackend::MockStreamBackend() {
  // We initialize the stream and pass it on to the debug agent, which will
  // think it's correctly connected to a client.
  stream_.set_writer(this);
  auto services = sys::ServiceDirectory::CreateFromNamespace();
  agent_ = std::make_unique<DebugAgent>(&stream_, std::move(services));
}

size_t MockStreamBackend::ConsumeStreamBufferData(const char* data,
                                                  size_t len) {
  // We assume we always get a header.
  const debug_ipc::MsgHeader* header =
      reinterpret_cast<const debug_ipc::MsgHeader*>(data);

  // Buffer for the message and create a reader.
  std::vector<char> msg_buffer;
  msg_buffer.reserve(len);
  msg_buffer.insert(msg_buffer.end(), data, data + len);
  debug_ipc::MessageReader reader(std::move(msg_buffer));

  // Dispatch the messages we find interesting.
  // NOTE: Here is where you add more notification handlers as they are sent by
  //       the debug agent.
  uint32_t transaction = 0;
  switch (header->type) {
    case debug_ipc::MsgHeader::Type::kAttach: {
      debug_ipc::AttachReply attach;
      if (!debug_ipc::ReadReply(&reader, &attach, &transaction))
        FXL_NOTREACHED();
      HandleAttach(std::move(attach));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyException: {
      debug_ipc::NotifyException exception;
      if (!debug_ipc::ReadNotifyException(&reader, &exception))
        FXL_NOTREACHED();
      HandleNotifyException(std::move(exception));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyModules: {
      debug_ipc::NotifyModules modules;
      if (!debug_ipc::ReadNotifyModules(&reader, &modules))
        FXL_NOTREACHED();
      HandleNotifyModules(std::move(modules));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyProcessExiting: {
      debug_ipc::NotifyProcessExiting process;
      if (!debug_ipc::ReadNotifyProcessExiting(&reader, &process))
        FXL_NOTREACHED();
      HandleNotifyProcessExiting(std::move(process));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyProcessStarting: {
      debug_ipc::NotifyProcessStarting process;
      if (!debug_ipc::ReadNotifyProcessStarting(&reader, &process))
        FXL_NOTREACHED();
      HandleNotifyProcessStarting(std::move(process));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyThreadExiting: {
      debug_ipc::NotifyThread thread;
      if (!debug_ipc::ReadNotifyThread(&reader, &thread))
        FXL_NOTREACHED();
      HandleNotifyThreadExiting(std::move(thread));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyThreadStarting: {
      debug_ipc::NotifyThread thread;
      if (!debug_ipc::ReadNotifyThread(&reader, &thread))
        FXL_NOTREACHED();
      HandleNotifyThreadStarting(std::move(thread));
      break;
    }
    default:
      FXL_NOTREACHED() << "Unhandled notification: "
                       << debug_ipc::MsgHeader::TypeToString(header->type);
      break;
  }

  // Say we read the whole message.
  return len;
}

}  // namespace debug_agent
