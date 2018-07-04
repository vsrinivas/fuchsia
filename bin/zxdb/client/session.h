// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/system_impl.h"
#include "garnet/public/lib/fxl/memory/ref_ptr.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace debug_ipc {
class BufferedFD;
class StreamBuffer;
}

namespace zxdb {

class ArchInfo;
class ProcessImpl;
class RemoteAPI;
class RemoteAPIImpl;
class RemoteAPITest;
class ThreadImpl;

// The session object manages the connection with the remote debug agent.
class Session {
 public:
  // Creates a session with no connection. All sending will fail until
  // the callback associated with a Connect() call is issued.
  Session();

  // Creates a session using a custom RemoteAPI implementation. Use for tests
  // to mock out sending IPC messages.
  explicit Session(std::unique_ptr<RemoteAPI> remote_api);

  // Creates with a previously-allocated connection. The pointer must outlive
  // this class. In this mode, the stream can not be disconnected.
  explicit Session(debug_ipc::StreamBuffer* stream);

  ~Session();

  // The RempteAPI for sending messages to the debug_agent.
  RemoteAPI* remote_api() { return remote_api_.get(); }

  // Notification about the stream.
  void OnStreamReadable();
  void OnStreamError();

  // Returns true if there is currently a connection.
  bool IsConnected() const;

  // Connects to a remote system. Calling when there is already a connection
  // will issue the callback with an error.
  void Connect(const std::string& host, uint16_t port,
               std::function<void(const Err&)> callback);

  // Disconnects from the remote system. Calling when there is no connection
  // connection will issue the callback with an error.
  //
  // This can also be called when a connection is pending (Connect() has been
  // called but the callback has not been issued yet) which will cancel the
  // pending connection. The Connect() callback will still be issued but
  // will indicate failure.
  void Disconnect(std::function<void(const Err&)> callback);

  // Frees all connection-related data. A helper for different modes of
  // cleanup.
  void ClearConnectionData();

  // Access to the singleton corresponding to the debugged system.
  System& system() { return system_; }

  // Provide access to the underlying system implementation. This is needed
  // for some client tests, but should not be used outside of the client
  // directory.
  //
  // TODO(brettw) probably this class needs to be separated into Session and
  // SessionImpl and which one of those you have controls which System object
  // you can get.
  SystemImpl& system_impl() { return system_; }

  // Architecture of the attached system. Will be "kUnknown" when not
  // connected.
  debug_ipc::Arch arch() const { return arch_; }

  // Architecture information of the attached system. Will be null when not
  // connected.
  const ArchInfo* arch_info() const { return arch_info_.get(); }

 private:
  class PendingConnection;
  friend PendingConnection;
  friend RemoteAPIImpl;
  friend RemoteAPITest;

  // Nonspecific callback type. Implemented by SessionDispatchCallback (with
  // the type-specific parameter pre-bound). The uint32_t is the transaction
  // ID. If the error is set, the data will be invalid and the callback should
  // be issued with the error instead of trying to deserialize.
  using Callback = std::function<void(const Err&, std::vector<char>)>;

  // Dispatches unsolicited notifications sent from the agent.
  void DispatchNotification(const debug_ipc::MsgHeader& header,
                            std::vector<char> data);
  void DispatchNotifyThread(debug_ipc::MsgHeader::Type type,
                            const debug_ipc::NotifyThread& notify);
  void DispatchNotifyException(const debug_ipc::NotifyException& notify);

  // Returns the thread object from the given koids, or null.
  ThreadImpl* ThreadImplFromKoid(uint64_t process_koid, uint64_t thread_koid);

  // Callback when a connection has been successful or failed.
  void ConnectionResolved(fxl::RefPtr<PendingConnection> pending,
                          const Err& err, const debug_ipc::HelloReply& reply,
                          std::unique_ptr<debug_ipc::BufferedFD> buffer,
                          std::function<void(const Err&)> callback);

  // Non-owning pointer to the connected stream. If this is non-null and
  // connection_storage_ is null, the connection is persistent (made via the
  // constructor) and can't be disconnected.
  //
  // This could be null when the connection_storage_ isn't when we're waiting
  // for the initial connection.
  debug_ipc::StreamBuffer* stream_ = nullptr;

  std::unique_ptr<RemoteAPI> remote_api_;

  // When using non-persistent connections (no connection passed in via the
  // constructor), this will hold the underlying OS connection that is used
  // to back stream_.
  //
  // Code should use stream_ for sending and receiving.
  std::unique_ptr<debug_ipc::BufferedFD> connection_storage_;

  // When a connection has been requested but is being connected on the
  // background thread, this will hold the pointer.
  fxl::RefPtr<PendingConnection> pending_connection_;

  std::map<uint32_t, Callback> pending_;
  uint32_t next_transaction_id_ = 1;  // Reserve 0 for notifications.

  SystemImpl system_;

  debug_ipc::Arch arch_ = debug_ipc::Arch::kUnknown;
  std::unique_ptr<ArchInfo> arch_info_;

  fxl::WeakPtrFactory<Session> weak_factory_;
};

}  // namespace zxdb
