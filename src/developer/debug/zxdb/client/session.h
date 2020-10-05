// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SESSION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SESSION_H_

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/session_observer.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_ipc {
class BufferedFD;
class StreamBuffer;
}  // namespace debug_ipc

namespace zxdb {

class ArchInfo;
class BreakpointObserver;
class DownloadObserver;
class FilterObserver;
class ProcessImpl;
class ProcessObserver;
class RemoteAPI;
class RemoteAPIImpl;
class RemoteAPITest;
class TargetObserver;
class ThreadImpl;
class ThreadObserver;

enum class SessionConnectionType : uint32_t {
  kNetwork = 1,
  kUnix = 2,
};

struct SessionConnectionInfo {
  SessionConnectionType type = SessionConnectionType::kNetwork;

  // If the connection type is `Network` then `host` is the IP address or URL.
  // If the connection type is `Unix` then `host` is the file path to the socket.
  std::string host;

  // If the connection type is `Network` then `port` is the port address.
  // If the connection type is `Unix` then `port` is unused.
  uint16_t port = 0;
};

// The session object manages the connection with the remote debug agent.
class Session : public SettingStoreObserver {
 public:
  // Creates a session with no connection. All sending will fail until the callback associated with
  // a Connect() call is issued.
  Session();

  // Creates a session using a custom RemoteAPI implementation. Use for tests to mock out sending
  // IPC messages.
  Session(std::unique_ptr<RemoteAPI> remote_api, debug_ipc::Arch arch);

  // Creates with a previously-allocated connection. The pointer must outlive this class. In this
  // mode, the stream can not be disconnected.
  explicit Session(debug_ipc::StreamBuffer* stream);
  virtual ~Session();

  fxl::WeakPtr<Session> GetWeakPtr();

  // The RempteAPI for sending messages to the debug_agent.
  RemoteAPI* remote_api() { return remote_api_.get(); }

  void AddObserver(SessionObserver* observer);
  void RemoveObserver(SessionObserver* observer);

  void AddBreakpointObserver(BreakpointObserver* observer);
  void RemoveBreakpointObserver(BreakpointObserver* observer);

  void AddFilterObserver(FilterObserver* observer);
  void RemoveFilterObserver(FilterObserver* observer);

  void AddDownloadObserver(DownloadObserver* observer);
  void RemoveDownloadObserver(DownloadObserver* observer);

  // Returns information about whether this session is connected to a minidump instead of a live
  // system.
  bool is_minidump() const { return is_minidump_; }

  // Notification about the stream.
  void OnStreamReadable();
  void OnStreamError();

  // Returns true if there is currently a connection.
  bool IsConnected() const;

  // Information about the current connection.
  const std::string minidump_path() const { return minidump_path_; }
  const std::string connected_host() const { return connected_info_.host; }
  uint16_t connected_port() const { return connected_info_.port; }

  // Call with an empty host and 0 port to reconnect to the last attempted connection destination.
  // If there is no previous destination, this will be issue an error.
  void Connect(const SessionConnectionInfo& info, fit::callback<void(const Err&)> cb);

  // Disconnects from the remote system. Calling when there is no connection connection will issue
  // the callback with an error.
  //
  // This can also be called when a connection is pending (Connect() has been called but the
  // callback has not been issued yet) which will cancel the pending connection. The Connect()
  // callback will still be issued but will indicate failure.
  void Disconnect(fit::callback<void(const Err&)> callback);

  // Open a minidump instead of connecting to a running system. The callback will be issued with an
  // error if the file cannot be opened or if there is already a connection.
  void OpenMinidump(const std::string& path, fit::callback<void(const Err&)> callback);

  // Frees all connection-related data. A helper for different modes of cleanup. Returns true if
  // there was a connection to clear.
  bool ClearConnectionData();

  // Access to the singleton corresponding to the debugged system.
  System& system() { return system_; }

  // Architecture of the attached system. Will be "kUnknown" when not connected.
  debug_ipc::Arch arch() const { return arch_; }

  // Architecture information of the attached system. Will be null when not connected.
  const ArchInfo* arch_info() const { return arch_info_.get(); }

  // Observer list getters.
  fxl::ObserverList<TargetObserver>& target_observers() { return target_observers_; }
  fxl::ObserverList<ProcessObserver>& process_observers() { return process_observers_; }
  fxl::ObserverList<ThreadObserver>& thread_observers() { return thread_observers_; }
  fxl::ObserverList<BreakpointObserver>& breakpoint_observers() { return breakpoint_observers_; }
  fxl::ObserverList<FilterObserver>& filter_observers() { return filter_observers_; }
  fxl::ObserverList<DownloadObserver>& download_observers() { return download_observers_; }

  // When the client tells the agent to launch a component, it will return an unique id identifying
  // that launch. Later, when the component effectively starts, the session will use that ID to know
  // which component it is.
  void ExpectComponent(uint32_t component_id);

  // Dispatches these particular notification types from the agent. These are public since tests
  // will commonly want to synthesize these events.
  //
  // Note on DispatchNotifyException: Test code can skip setting the metadata by clearing the
  // set_metadata flag. This allows them to set up the thread's state manually before issuing an
  // exception. Production code should always set the set_metadata flag to populate the thread's
  // state from the data in the exception.
  void DispatchNotifyThreadStarting(const debug_ipc::NotifyThread& notify);
  void DispatchNotifyThreadExiting(const debug_ipc::NotifyThread& notify);
  void DispatchNotifyException(const debug_ipc::NotifyException& notify, bool set_metadata = true);
  void DispatchNotifyModules(const debug_ipc::NotifyModules& notify);
  void DispatchProcessStarting(const debug_ipc::NotifyProcessStarting&);
  void DispatchNotifyIO(const debug_ipc::NotifyIO& notify);

  // Sends an explicit quit cmd to the agent.
  void QuitAgent(fit::callback<void(const Err&)> callback);

  // SettingStoreObserver
  void OnSettingChanged(const SettingStore&, const std::string& setting_name) override;

  // For test purposes, so that the Session appears to be connected.
  void set_stream(debug_ipc::StreamBuffer* stream) { stream_ = stream; }

 protected:
  fxl::ObserverList<SessionObserver> observers_;

 private:
  class PendingConnection;
  friend PendingConnection;
  friend RemoteAPIImpl;
  friend RemoteAPITest;

  // Nonspecific callback type. Implemented by SessionDispatchCallback (with the type-specific
  // parameter pre-bound). The uint32_t is the transaction ID. If the error is set, the data will be
  // invalid and the callback should be issued with the error instead of trying to deserialize.
  using Callback = fit::callback<void(const Err&, std::vector<char>)>;

  // Set the arch_ and arch_info_ fields.
  Err SetArch(debug_ipc::Arch arch);

  // Checks whether it's safe to begin establishing a connection. If not, the callback is invoked
  // with details. The opening_dump argument indicates whether we are trying to open a dump file
  // rather than connect to a debug agent.
  bool ConnectCanProceed(fit::callback<void(const Err&)>& callback, bool opening_dump);

  // Dispatches unsolicited notifications sent from the agent.
  void DispatchNotification(const debug_ipc::MsgHeader& header, std::vector<char> data);

  // Returns the thread object from the given koids, or null.
  ThreadImpl* ThreadImplFromKoid(uint64_t process_koid, uint64_t thread_koid);

  // Callback when a connection has been successful or failed.
  void ConnectionResolved(fxl::RefPtr<PendingConnection> pending, const Err& err,
                          const debug_ipc::HelloReply& reply,
                          std::unique_ptr<debug_ipc::BufferedFD> buffer,
                          fit::callback<void(const Err&)> callback);

  // Sends a notification to all the UI observers.
  [[gnu::format(printf, 3, 4)]] void SendSessionNotification(SessionObserver::NotificationType,
                                                             const char* fmt, ...);
  void SendSessionNotification(SessionObserver::NotificationType, const std::string& msg);

  SessionObserver::NotificationType HandleProcessIO(ProcessImpl*, const debug_ipc::NotifyIO&);
  void ListenForSystemSettings();

  // Configurations --------------------------------------------------------------------------------

  // Upon connection, the sessino will tell the agent of all the configurations it should know
  // about.
  void SendAgentConfiguration();

  // Pushes the quit agent config action into |actions|.
  void ConfigQuitAgent(bool quit, std::vector<debug_ipc::ConfigAction>* actions);

  // Whether we have opened a core dump. Makes much of the connection-related stuff obsolete.
  bool is_minidump_ = false;

  // Observers.
  fxl::ObserverList<TargetObserver> target_observers_;
  fxl::ObserverList<ProcessObserver> process_observers_;
  fxl::ObserverList<ThreadObserver> thread_observers_;
  fxl::ObserverList<BreakpointObserver> breakpoint_observers_;
  fxl::ObserverList<FilterObserver> filter_observers_;
  fxl::ObserverList<DownloadObserver> download_observers_;

  // Non-owning pointer to the connected stream. If this is non-null and connection_storage_ is
  // null, the connection is persistent (made via the constructor) and can't be disconnected.
  //
  // This could be null when the connection_storage_ isn't when we're waiting for the initial
  // connection.
  debug_ipc::StreamBuffer* stream_ = nullptr;

  std::unique_ptr<RemoteAPI> remote_api_;

  // When using non-persistent connections (no connection passed in via the constructor), this will
  // hold the underlying OS connection that is used to back stream_ as well as the
  //
  // Code should use stream_ for sending and receiving.
  std::unique_ptr<debug_ipc::BufferedFD> connection_storage_;

  // Stores what the session is currently connected to.
  std::string minidump_path_;
  SessionConnectionInfo connected_info_;

  // When a connection has been requested but is being connected on the background thread, this will
  // hold the pointer.
  fxl::RefPtr<PendingConnection> pending_connection_;

  std::map<uint32_t, Callback> pending_;
  uint32_t next_transaction_id_ = 1;  // Reserve 0 for notifications.

  // Component ids that the session is currently waiting on. See ExpectComponent comments for more
  // information on these ids.
  std::set<uint32_t> expected_components_;

  System system_;

  debug_ipc::Arch arch_ = debug_ipc::Arch::kUnknown;
  std::unique_ptr<ArchInfo> arch_info_;

  // The last connection that was made by the session. Will have an empty host and a 0 port
  // if there has never been a connection.
  SessionConnectionInfo last_connection_;

  fxl::WeakPtrFactory<Session> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SESSION_H_
