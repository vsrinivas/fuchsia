// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/local_stream_backend.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

LocalStreamBackend::LocalStreamBackend() { stream_.set_writer(this); }

LocalStreamBackend::~LocalStreamBackend() = default;

size_t LocalStreamBackend::ConsumeStreamBufferData(const char* data, size_t len) {
  // We assume we always get a header.
  const debug_ipc::MsgHeader* header = reinterpret_cast<const debug_ipc::MsgHeader*>(data);

  // Buffer for the message and create a reader.
  std::vector<char> msg_buffer;
  msg_buffer.reserve(len);
  msg_buffer.insert(msg_buffer.end(), data, data + len);

  // Dispatch the messages we find interesting.
  // NOTE: Here is where you add more notification handlers as they are sent by
  //       the debug agent.
  DEBUG_LOG(Test) << "Got notification: " << debug_ipc::MsgHeader::TypeToString(header->type);
  switch (header->type) {
    case debug_ipc::MsgHeader::Type::kAttach: {
      debug_ipc::AttachReply attach;
      uint32_t transaction = 0;
      if (!debug_ipc::Deserialize(std::move(msg_buffer), &attach, &transaction))
        FX_NOTREACHED();
      HandleAttach(std::move(attach));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyException: {
      debug_ipc::NotifyException exception;
      if (!debug_ipc::DeserializeNotifyException(std::move(msg_buffer), &exception))
        FX_NOTREACHED();
      HandleNotifyException(std::move(exception));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyIO: {
      debug_ipc::NotifyIO io;
      if (!debug_ipc::DeserializeNotifyIO(std::move(msg_buffer), &io))
        FX_NOTREACHED();
      HandleNotifyIO(std::move(io));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyModules: {
      debug_ipc::NotifyModules modules;
      if (!debug_ipc::DeserializeNotifyModules(std::move(msg_buffer), &modules))
        FX_NOTREACHED();
      HandleNotifyModules(std::move(modules));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyProcessExiting: {
      debug_ipc::NotifyProcessExiting process;
      if (!debug_ipc::DeserializeNotifyProcessExiting(std::move(msg_buffer), &process))
        FX_NOTREACHED();
      HandleNotifyProcessExiting(std::move(process));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyProcessStarting: {
      debug_ipc::NotifyProcessStarting process;
      if (!debug_ipc::DeserializeNotifyProcessStarting(std::move(msg_buffer), &process))
        FX_NOTREACHED();
      HandleNotifyProcessStarting(std::move(process));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyThreadExiting: {
      debug_ipc::NotifyThread thread;
      if (!debug_ipc::DeserializeNotifyThreadExiting(std::move(msg_buffer), &thread))
        FX_NOTREACHED();
      HandleNotifyThreadExiting(std::move(thread));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyThreadStarting: {
      debug_ipc::NotifyThread thread;
      if (!debug_ipc::DeserializeNotifyThreadStarting(std::move(msg_buffer), &thread))
        FX_NOTREACHED();
      HandleNotifyThreadStarting(std::move(thread));
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyLog: {
      debug_ipc::NotifyLog log;
      if (!debug_ipc::DeserializeNotifyLog(std::move(msg_buffer), &log))
        FX_NOTREACHED();
      HandleNotifyLog(std::move(log));
      break;
    }
    default:
      FX_NOTREACHED() << "Unhandled notification: "
                      << debug_ipc::MsgHeader::TypeToString(header->type);
      break;
  }

  // Say we read the whole message.
  return len;
}

}  // namespace debug_agent
