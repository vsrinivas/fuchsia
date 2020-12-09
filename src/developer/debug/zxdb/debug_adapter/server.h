// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_SERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_SERVER_H_

#include <thread>

#include "src/developer/debug/shared/buffered_fd.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/debug_adapter/context.h"

namespace zxdb {

class DebugAdapterContext;

// Observer interface for DebugAdapterServer. Mainly used in tests.
class DebugAdapterServerObserver {
 public:
  virtual void ClientConnected() {}
  virtual void ClientDisconnected() {}
};

// Waits for a single client connection and creates debug adapter context to cater to client
// requests. Monitors the socket for connection loss and restarts the process of waiting for
// incoming connections.
class DebugAdapterServer {
 public:
  DebugAdapterServer(Session* session, uint16_t port) : session_(session), port_(port) {}

  ~DebugAdapterServer();

  // Setup server and wait for incoming connections on a background thread.
  Err Init();

  bool IsConnected() { return !!buffer_; }

  void AddObserver(DebugAdapterServerObserver* observer) { observers_.AddObserver(observer); }
  void RemoveObserver(DebugAdapterServerObserver* observer) { observers_.RemoveObserver(observer); }

 private:
  Session* session_;
  uint16_t port_;

  fbl::unique_fd server_socket_;
  debug_ipc::MessageLoop* main_loop_ = nullptr;

  std::unique_ptr<std::thread> background_thread_;
  bool background_thread_exit_ = false;
  std::unique_ptr<DebugAdapterContext> context_;
  std::unique_ptr<debug_ipc::BufferedFD> buffer_;

  fxl::ObserverList<DebugAdapterServerObserver> observers_;

  // NOTE: Only this method is executed on the background thread. All other methods must be called
  // from main thread.
  void ListenBackgroundThread();

  // Launch background thread and wait for new connections.
  void ListenConnection();

  // Create debug adapter context to cater to the client requests.
  void ConnectionResolvedMainThread(fbl::unique_fd client);

  bool Accept(fbl::unique_fd& client);

  void OnConnectionError();

  void ResetClientConnection();
  FXL_DISALLOW_COPY_AND_ASSIGN(DebugAdapterServer);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_SERVER_H_
