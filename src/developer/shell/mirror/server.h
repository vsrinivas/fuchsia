// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <set>

#include "src/developer/debug/shared/buffered_fd.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/shell/mirror/common.h"
#include "src/developer/shell/mirror/wire_format.h"

#ifndef SRC_DEVELOPER_SHELL_MIRROR_SERVER_H_
#define SRC_DEVELOPER_SHELL_MIRROR_SERVER_H_

namespace shell::mirror {

class SocketServer;

// Represents a instance of a connection "attempt" to a client.
class SocketConnection {
 public:
  explicit SocketConnection(SocketServer* server) : server_(server), id_(global_id_++) {}
  ~SocketConnection();

  SocketConnection& operator=(SocketConnection&) = delete;
  SocketConnection(SocketConnection&) = delete;

  // |main_thread_loop| is used for posting a task that creates the debug agent after accepting a
  // a connection. This is because the debug agent assumes it's running on the message loop's
  // thread.
  Err Accept(debug_ipc::MessageLoop* main_thread_loop, int server_fd);

  struct SocketConnectionComparator {
    bool operator()(const SocketConnection& one, const SocketConnection& two) {
      return one.id_ < two.id_;
    }
  };

  // Unregisters this socket connection from a given server.  This has the effect
  // of deleting the connection, so use with caution.
  void UnregisterAndDestroy();

 private:
  static uint64_t global_id_;

  SocketServer* server_;
  debug_ipc::BufferedFD buffer_;
  bool connected_ = false;
  uint64_t id_;
};

// Represents a server.
class SocketServer : public debug_ipc::FDWatcher {
 public:
  SocketServer() = default;

  // A configuration object for the server.
  struct ConnectionConfig {
    debug_ipc::PlatformMessageLoop* message_loop = nullptr;
    int port = 0;
    std::optional<std::string> path;
  };

  // Runs the server with the given configuration.
  void Run(ConnectionConfig config);

  // Sets up loops in a sensible way (one loop to accept a connection, and one loop to respond to
  // requests), and runs a server.  Calls inited_fn when it is done initing.
  Err RunInLoop(ConnectionConfig config, debug_ipc::FileLineFunction from_here,
                fit::closure inited_fn);

  int GetPort() { return config_.port; }
  std::string GetPath() { return *config_.path; }

  void RemoveConnection(SocketConnection* connection) {
    for (auto it = connections_.begin(); it != connections_.end(); ++it) {
      if (it->get() == connection) {
        connections_.erase(it);
        return;
      }
    }
  }

  // IMPORTANT: All others can only be called on the main thread.

  // Initialize the server.
  // |port| is the port to use.  If *port is 0, the function will try to assign one.
  Err Init(uint16_t* port);

  virtual void OnFDReady(int fd, bool read, bool write, bool err) override;

 private:
  fbl::unique_fd server_socket_;
  std::set<std::unique_ptr<SocketConnection>> connections_;
  debug_ipc::MessageLoop::WatchHandle connection_monitor_;
  ConnectionConfig config_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketServer);
};

// Manages sending data along a given StreamBuffer.
class Update {
 public:
  Update(debug_ipc::StreamBuffer* stream, const std::string* path)
      : stream_(stream), files_(*path), path_(*path) {}

  // Sends the contents of |path| to |stream|.
  Err SendUpdates();

 private:
  debug_ipc::StreamBuffer* stream_;
  Files files_;
  std::string path_;
};

}  // namespace shell::mirror

#endif  // SRC_DEVELOPER_SHELL_MIRROR_SERVER_H_
