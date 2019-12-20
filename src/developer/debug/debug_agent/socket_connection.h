// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SOCKET_CONNECTION_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SOCKET_CONNECTION_H_

#include "lib/sys/cpp/service_directory.h"
#include "src/developer/debug/debug_agent/remote_api_adapter.h"
#include "src/developer/debug/shared/buffered_fd.h"
#include "src/developer/debug/shared/platform_message_loop.h"

namespace debug_agent {

class DebugAgent;
class SocketConnection;

// SocketServer ------------------------------------------------------------------------------------
//
// Listens for connections on a socket. Only one connection is supported at a
// time. It waits for connections in a blocking fashion, and then runs the
// message loop on that connection.
//
// IMPORTANT: This class is being used to accept connections on a background thread in order to let
//            the message loop run. But a lot of code (from the DebugAgent down) assumes that it's
//            running on the main thread, so it's important to know on which thread each call is
//            made.
//
//            The criteria is as follows:
//
//            Only call Run on background thread.
//            All other must be called on the main thread.
//
// NOTE: No synchronization is needed because all the connection/agent management occurs on the
//       main thread. The only thing that's done on the background thread is accepting the
//       connection. After that, the actual agent creation is posted to the message loop.
class SocketServer {
 public:
  SocketServer() = default;

  // All fields of |config| must be correctly set.
  //
  // IMPORTANT: Only this can be called on another thread.
  //            We use |main_thread_loop| to post a task that actually creates the debug agent on
  //            the main thread after the connection has been made. This is because the agent has a
  //            lot of assumptions of being run on the thread of the message loop.
  struct ConnectionConfig {
    debug_ipc::PlatformMessageLoop* message_loop = nullptr;
    debug_agent::DebugAgent* debug_agent = nullptr;
    int port = 0;
  };
  void Run(ConnectionConfig config);

  // IMPORTANT: All others can only be called on the main thread.

  bool Init(uint16_t port);
  // Call before consecutive calls to |Run|.
  void Reset();

  bool connected() const { return !!connection_; }

 private:
  fbl::unique_fd server_socket_;
  std::unique_ptr<SocketConnection> connection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketServer);
};

// Socket Connection -------------------------------------------------------------------------------
//
// Represents a instance of a connection "attempt" to a client.
// It owns a DebugAgent, which is currently always associated to one connection.
class SocketConnection {
 public:
  SocketConnection(debug_agent::DebugAgent*);
  ~SocketConnection();

  // |main_thread_loop| is used for posting a task that creates the debug agent after accepting a
  // a connection. This is because the debug agent assumes it's running on the message loop's
  // thread.
  bool Accept(debug_ipc::MessageLoop* main_thread_loop, int server_fd);

 private:
  debug_agent::DebugAgent* debug_agent_ = nullptr;

  std::unique_ptr<debug_agent::RemoteAPIAdapter> adapter_;
  debug_ipc::BufferedFD buffer_;
  bool connected_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketConnection);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SOCKET_CONNECTION_H_
