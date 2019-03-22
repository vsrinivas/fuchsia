// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/ipc/client_protocol.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/stream_buffer.h"

namespace debug_ipc {
class MessageReader;
}  // namespace debug_ipc

namespace debug_agent {

// This class is meant to receive the raw messages outputted by the debug agent.
// The agent's stream calls this backend to output the data and verifies that
// all the content is sent.
//
// We use this class to intercept the messages sent back from the agent and
// react accordingly. This class is kinda hardcoded for this tests, as different
// integration tests care about different messages. If there are more tests that
// require this kind of interception, this class should be separated and
// generalized.
class MockStreamBackend : public debug_ipc::StreamBuffer::Writer {
 public:
  MockStreamBackend();
  RemoteAPI* remote_api() { return agent_.get(); }
  DebugAgent* agent() { return agent_.get(); }

  // Message dispatcher interface.
  // This should be overriden by every test interested in a particular set of
  // messages. By default they do nothing.
  virtual void HandleAttach(debug_ipc::AttachReply) {}
  virtual void HandleNotifyException(debug_ipc::NotifyException) {}
  virtual void HandleNotifyModules(debug_ipc::NotifyModules) {}
  virtual void HandleNotifyProcessExiting(debug_ipc::NotifyProcessExiting) {}
  virtual void HandleNotifyProcessStarting(debug_ipc::NotifyProcessStarting) {}
  virtual void HandleNotifyThreadExiting(debug_ipc::NotifyThread) {}
  virtual void HandleNotifyThreadStarting(debug_ipc::NotifyThread) {}

  // The stream will call this function to send the data to whatever backend it
  // is connected to. It returns how much of the input message it could actually
  // write. For this tests purposes, we always read the whole message.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

 private:
  // This is the stream the debug agent will be given to write to.
  debug_ipc::StreamBuffer stream_;
  std::unique_ptr<DebugAgent> agent_;
};

}  // namespace debug_agent
