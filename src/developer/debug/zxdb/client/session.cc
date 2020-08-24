// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/session.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>
#include <variant>

#include "src/developer/debug/ipc/client_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/buffered_fd.h"
#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/breakpoint_action.h"
#include "src/developer/debug/zxdb/client/breakpoint_impl.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/minidump_remote_api.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/remote_api_impl.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/socket_connect.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/client/thread_impl.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Max message size before considering it corrupt. This is very large so we can send nontrivial
// memory dumps over the channel, but ensures we won't crash trying to allocate an unreasonable
// buffer size if the stream is corrupt.
constexpr uint32_t kMaxMessageSize = 16777216;

// Remove conditional breakpoints from stop_info; return whether we'll need to skip this stop_info
// and continue execution, which happens when the exception is a breakpoint one and all breakpoints
// in it are conditional.
bool FilterConditionalBreakpoints(StopInfo* info) {
  bool skip = false;

  if (info->exception_type == debug_ipc::ExceptionType::kHardwareBreakpoint ||
      info->exception_type == debug_ipc::ExceptionType::kWatchpoint ||
      info->exception_type == debug_ipc::ExceptionType::kSoftwareBreakpoint) {
    // It's possible that hit_breakpoints is empty even when exception_type is kSoftware,
    // e.g. the process explicitly called "int 3" on x64. In this case, we should still pause.
    if (!info->hit_breakpoints.empty()) {
      skip = true;
    }
  }

  auto breakpoint_iter = info->hit_breakpoints.begin();
  while (breakpoint_iter != info->hit_breakpoints.end()) {
    Breakpoint* breakpoint = breakpoint_iter->get();
    // TODO(dangyi): Consider whether to move the condition to Breakpoint class.
    if (breakpoint->GetStats().hit_count % breakpoint->GetSettings().hit_mult == 0) {
      skip = false;
      breakpoint_iter++;
    } else {
      info->hit_breakpoints.erase(breakpoint_iter);
    }
  }

  return skip;
}

}  // namespace

// PendingConnection -----------------------------------------------------------

// Storage for connection information when connecting dynamically. Making a connection has three
// asynchronous steps:
//
//  1. Resolving the host and connecting the socket. Since this is blocking, it happens on a
//     background thread.
//  2. Sending the hello message. Happens on the main thread.
//  3. Waiting for the reply and deserializing, then notifying the Session.
//
// Various things can happen in the middle.
//
//  - Any step can fail.
//  - The Session object can be destroyed (weak pointer checks).
//  - The connection could be canceled by the user (the session callback checks for this).
class Session::PendingConnection : public fxl::RefCountedThreadSafe<PendingConnection> {
 public:
  void Initiate(fxl::WeakPtr<Session> session, fit::callback<void(const Err&)> callback);

  // There are no other functions since this will be running on a background thread and the class
  // state can't be safely retrieved. It reports all of the output state via ConnectionResolved.

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(PendingConnection);
  FRIEND_MAKE_REF_COUNTED(PendingConnection);

  PendingConnection(const SessionConnectionInfo& info) : connection_info_(info) {}
  ~PendingConnection() {}

  // These are the steps of connection, in order. They each take a RefPtr to |this| to ensure the
  // class is in scope for the full flow.
  void ConnectBackgroundThread(fxl::RefPtr<PendingConnection> owner);
  void ConnectCompleteMainThread(fxl::RefPtr<PendingConnection> owner, const Err& err);
  void DataAvailableMainThread(fxl::RefPtr<PendingConnection> owner);
  void HelloCompleteMainThread(fxl::RefPtr<PendingConnection> owner, const Err& err,
                               const debug_ipc::HelloReply& reply);

  // Creates the connection (called on the background thread). On success the socket_ is populated.
  Err DoConnectBackgroundThread();

  SessionConnectionInfo connection_info_;

  // Only non-null when in the process of connecting.
  std::unique_ptr<std::thread> thread_;

  debug_ipc::MessageLoop* main_loop_ = nullptr;

  // Access only on the main thread.
  fxl::WeakPtr<Session> session_;

  // The constructed socket and buffer.
  //
  // The socket is created by ConnectBackgroundThread and read by HelloCompleteMainThread to create
  // the buffer so needs no synchronization. It would be cleaner to pass this in the lambdas to
  // avoid threading confusion, but move-only types can't be bound.
  fbl::unique_fd socket_;
  std::unique_ptr<debug_ipc::BufferedFD> buffer_;

  // Callback when the connection is complete (or fails). Access only on the main thread.
  fit::callback<void(const Err&)> callback_;
};

void Session::PendingConnection::Initiate(fxl::WeakPtr<Session> session,
                                          fit::callback<void(const Err&)> callback) {
  FX_DCHECK(!thread_.get());  // Duplicate Initiate() call.

  main_loop_ = debug_ipc::MessageLoop::Current();
  session_ = std::move(session);
  callback_ = std::move(callback);

  // Create the background thread, and run the background function. The context will keep a ref to
  // this class.
  thread_ = std::make_unique<std::thread>(
      [owner = fxl::RefPtr<PendingConnection>(this)]() { owner->ConnectBackgroundThread(owner); });
}

void Session::PendingConnection::ConnectBackgroundThread(fxl::RefPtr<PendingConnection> owner) {
  Err err = DoConnectBackgroundThread();
  main_loop_->PostTask(FROM_HERE, [owner = std::move(owner), err]() {
    owner->ConnectCompleteMainThread(owner, err);
  });
}

void Session::PendingConnection::ConnectCompleteMainThread(fxl::RefPtr<PendingConnection> owner,
                                                           const Err& err) {
  // The background thread function has now completed so the thread can be destroyed. We do want to
  // join with the thread here to ensure there are no references to the PendingConnection on the
  // background thread, which might in turn cause the PendingConnection to be destroyed on the
  // background thread.
  thread_->join();
  thread_.reset();

  if (!session_ || err.has_error()) {
    // Error or session destroyed, skip sending hello and forward the error.
    HelloCompleteMainThread(owner, err, debug_ipc::HelloReply());
    return;
  }

  FX_DCHECK(socket_.is_valid());
  buffer_ = std::make_unique<debug_ipc::BufferedFD>();
  buffer_->Init(std::move(socket_));

  // The connection is now established, so we set up the handlers before we send the first request
  // over to the agent. Even though we're in a message loop and these handlers won't be called
  // within this stack frame, it's a good mental model to set up handlers before actually sending
  // the first piece of data.
  buffer_->set_data_available_callback([owner]() { owner->DataAvailableMainThread(owner); });
  buffer_->set_error_callback([owner]() {
    owner->HelloCompleteMainThread(owner, Err("Connection error."), debug_ipc::HelloReply());
  });

  // Send "Hello" message. We can't use the Session::Send infrastructure since the connection hasn't
  // technically been established yet.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteRequest(debug_ipc::HelloRequest(), 1, &writer);
  buffer_->stream().Write(writer.MessageComplete());
}

void Session::PendingConnection::DataAvailableMainThread(fxl::RefPtr<PendingConnection> owner) {
  // This function needs to manually deserialize the hello message since the Session stuff isn't
  // connected yet.
  constexpr size_t kHelloMessageSize =
      debug_ipc::MsgHeader::kSerializedHeaderSize + sizeof(debug_ipc::HelloReply);

  if (!buffer_->stream().IsAvailable(kHelloMessageSize))
    return;  // Wait for more data.

  std::vector<char> serialized;
  serialized.resize(kHelloMessageSize);
  buffer_->stream().Read(&serialized[0], kHelloMessageSize);

  debug_ipc::HelloReply reply;
  uint32_t transaction_id = 0;
  debug_ipc::MessageReader reader(std::move(serialized));

  Err err;
  if (!debug_ipc::ReadReply(&reader, &reply, &transaction_id) ||
      reply.signature != debug_ipc::HelloReply::kStreamSignature) {
    // Corrupt.
    err = Err("Corrupted reply, service is probably not the debug agent.");
    reply = debug_ipc::HelloReply();
  }

  HelloCompleteMainThread(owner, err, reply);
}

void Session::PendingConnection::HelloCompleteMainThread(fxl::RefPtr<PendingConnection> owner,
                                                         const Err& err,
                                                         const debug_ipc::HelloReply& reply) {
  // Prevent future notifications.
  if (buffer_.get()) {
    buffer_->set_data_available_callback({});
    buffer_->set_error_callback({});
  }

  if (session_) {
    // The buffer must be created here on the main thread since it will register with the message
    // loop to watch the FD.

    // If the session exists, always tell it about the completion, whether the connection was
    // successful or not. It will issue the callback.
    session_->ConnectionResolved(std::move(owner), err, reply, std::move(buffer_),
                                 std::move(callback_));
  } else if (callback_) {
    // Session was destroyed. Issue the callback with an error (not clobbering an existing one if
    // there was one).
    if (err.has_error())
      callback_(err);
    else
      callback_(Err("Session was destroyed."));
  }
}

Err Session::PendingConnection::DoConnectBackgroundThread() {
  switch (connection_info_.type) {
    case SessionConnectionType::kNetwork:
      return ConnectToHost(connection_info_.host, connection_info_.port, &socket_);
    case SessionConnectionType::kUnix:
      return ConnectToUnixSocket(connection_info_.host, &socket_);
  }
  FX_NOTREACHED();
  return Err("Unsupported Connection type");
}

// Session ---------------------------------------------------------------------

Session::Session()
    : remote_api_(std::make_unique<RemoteAPIImpl>(this)), system_(this), weak_factory_(this) {
  ListenForSystemSettings();
}

Session::Session(std::unique_ptr<RemoteAPI> remote_api, debug_ipc::Arch arch)
    : remote_api_(std::move(remote_api)), system_(this), arch_(arch), weak_factory_(this) {
  arch_info_ = std::make_unique<ArchInfo>();
  Err err = arch_info_->Init(arch);

  // Should not fail for synthetically set-up architectures.
  FX_DCHECK(!err.has_error());

  ListenForSystemSettings();
}

Session::Session(debug_ipc::StreamBuffer* stream)
    : stream_(stream), system_(this), weak_factory_(this) {
  ListenForSystemSettings();
}

Session::~Session() = default;

void Session::OnStreamReadable() {
  if (!stream_)
    return;  // Notification could have raced with detaching the stream.

  while (true) {
    if (!stream_ || !stream_->IsAvailable(debug_ipc::MsgHeader::kSerializedHeaderSize))
      return;

    std::vector<char> serialized_header;
    serialized_header.resize(debug_ipc::MsgHeader::kSerializedHeaderSize);
    stream_->Peek(&serialized_header[0], debug_ipc::MsgHeader::kSerializedHeaderSize);

    debug_ipc::MessageReader reader(std::move(serialized_header));
    debug_ipc::MsgHeader header;
    if (!reader.ReadHeader(&header)) {
      // Since we already validated there is enough data for the header, the header read should not
      // fail (it's just a memcpy).
      FX_NOTREACHED();
      return;
    }

    // Sanity checking on the size to prevent crashes.
    if (header.size > kMaxMessageSize) {
      SendSessionNotification(SessionObserver::NotificationType::kError,
                              "Bad message received of size %u.\n(type = %u, "
                              "transaction = %u)\n",
                              static_cast<unsigned>(header.size),
                              static_cast<unsigned>(header.type),
                              static_cast<unsigned>(header.transaction_id));
      // TODO(brettw) close the stream due to this fatal error.
      return;
    }

    if (!stream_->IsAvailable(header.size))
      return;  // Wait for more data.

    // Consume the message now that we know the size. Do this before doing anything else so the data
    // is consumed if the size is right, even if the transaction ID is wrong.
    std::vector<char> serialized;
    serialized.resize(header.size);
    stream_->Read(&serialized[0], header.size);

    // Transaction ID 0 is reserved for notifications.
    if (header.transaction_id == 0) {
      DispatchNotification(header, std::move(serialized));
      continue;
    }

    // Find the transaction.
    auto found = pending_.find(header.transaction_id);
    if (found == pending_.end()) {
      SendSessionNotification(SessionObserver::NotificationType::kError,
                              "Received reply for unexpected transaction %u (type = %u).\n",
                              static_cast<unsigned>(header.transaction_id),
                              static_cast<unsigned>(header.type));
      // Just ignore this bad message.
      continue;
    }

    // Do the type-specific deserialization and callback.
    found->second(Err(), std::move(serialized));

    pending_.erase(found);
  }
}

void Session::OnStreamError() {
  if (ClearConnectionData()) {
    SendSessionNotification(SessionObserver::NotificationType::kError,
                            "The debug agent has disconnected.\n"
                            "The system may have halted, or this may be a bug. "
                            "If you believe it is a bug, please file a report, "
                            "adding the system crash log (fx syslog) if possible.");
  }
}

bool Session::ConnectCanProceed(fit::callback<void(const Err&)>& callback, bool opening_dump) {
  Err err;
  if (stream_) {
    if (opening_dump) {
      err = Err("Cannot open a dump while connected to a debugged system.");
    } else {
      err = Err("Already connected.");
    }
  } else if (is_minidump_) {
    err = Err("A dump file is currently open.");
  } else if (pending_connection_.get()) {
    err = Err("A connection is already pending.");
  }

  if (err.has_error()) {
    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask(
          FROM_HERE, [callback = std::move(callback), err]() mutable { callback(err); });
    }
    return false;
  }

  return true;
}

bool Session::IsConnected() const { return stream_ != nullptr; }

void Session::Connect(const SessionConnectionInfo& info, fit::callback<void(const Err&)> cb) {
  if (!ConnectCanProceed(cb, false))
    return;

  if (info.host.empty() && last_connection_.host.empty()) {
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
      cb(Err("No previous destination to reconnect to."));
    });
    return;
  }

  connected_info_ = info;
  if (!connected_info_.host.empty()) {
    last_connection_ = info;
  }

  pending_connection_ = fxl::MakeRefCounted<PendingConnection>(last_connection_);
  pending_connection_->Initiate(weak_factory_.GetWeakPtr(), std::move(cb));
}

Err Session::SetArch(debug_ipc::Arch arch) {
  arch_info_ = std::make_unique<ArchInfo>();

  Err arch_err = arch_info_->Init(arch);
  if (!arch_err.has_error()) {
    arch_ = arch;
  }

  return arch_err;
}

void Session::OpenMinidump(const std::string& path, fit::callback<void(const Err&)> callback) {
  if (!ConnectCanProceed(callback, true)) {
    return;
  }

  is_minidump_ = true;
  minidump_path_ = path;
  remote_api_ = std::make_unique<MinidumpRemoteAPI>(this);
  auto minidump = reinterpret_cast<MinidumpRemoteAPI*>(remote_api_.get());
  Err err = minidump->Open(path);

  if (err.has_error()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback = std::move(callback), err]() mutable { callback(err); });

    return;
  }

  system().GetTargets()[0]->Attach(minidump->ProcessID(),
                                   [](fxl::WeakPtr<Target> target, const Err&) {});

  remote_api_->Hello(debug_ipc::HelloRequest(),
                     [callback = std::move(callback), weak_this = GetWeakPtr()](
                         const Err& err, debug_ipc::HelloReply reply) mutable {
                       if (weak_this && !err.has_error()) {
                         weak_this->SetArch(reply.arch);
                       }

                       callback(err);
                     });
}

void Session::Disconnect(fit::callback<void(const Err&)> callback) {
  if (!stream_ && !is_minidump_) {
    Err err;
    if (pending_connection_.get()) {
      // Cancel pending connection.
      pending_connection_ = nullptr;
    } else {
      err = Err("Not connected.");
    }

    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask(
          FROM_HERE, [callback = std::move(callback), err]() mutable { callback(err); });
    }
    return;
  }

  if (is_minidump_) {
    is_minidump_ = false;
    minidump_path_.clear();
    remote_api_ = std::make_unique<RemoteAPIImpl>(this);
  } else if (!connection_storage_) {
    // The connection is persistent (passed in via the constructor) and can't
    // be disconnected.
    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask(
          FROM_HERE, [callback = std::move(callback)]() mutable {
            callback(Err(ErrType::kGeneral,
                         "The connection can't be disconnected in this build "
                         "of the debugger."));
          });
      return;
    }
  }

  ClearConnectionData();
  if (callback)
    callback(Err());
}

bool Session::ClearConnectionData() {
  if (!connection_storage_)
    return false;

  stream_ = nullptr;
  connected_info_.host.clear();
  connected_info_.port = 0;
  arch_info_.reset();
  connection_storage_.reset();
  arch_ = debug_ipc::Arch::kUnknown;
  system_.DidDisconnect();
  return true;
}

void Session::DispatchNotifyThreadStarting(const debug_ipc::NotifyThread& notify) {
  ProcessImpl* process = system_.ProcessImplFromKoid(notify.record.process_koid);
  if (!process) {
    SendSessionNotification(SessionObserver::NotificationType::kWarning,
                            "Received thread starting notification for an "
                            "unexpected process %" PRIu64 ".\n",
                            notify.record.process_koid);
    return;
  }

  // If this is the initial thread, we need to check if we need to resume it depending on the user
  // defined setting for new processes.
  auto threads = process->GetThreads();

  bool resume_thread = false;
  if (!threads.empty()) {
    resume_thread = true;
  } else {
    // Depending on how the process was started, we need to see what setting is the one that
    // determines the behaviour.
    switch (process->start_type()) {
      case Process::StartType::kComponent:
      case Process::StartType::kLaunch:
        resume_thread = !system_.settings().GetBool(ClientSettings::System::kPauseOnLaunch);
        break;
      case Process::StartType::kAttach:
        resume_thread = !system_.settings().GetBool(ClientSettings::System::kPauseOnAttach);
        break;
    }
  }

  process->OnThreadStarting(notify.record, resume_thread);
}

void Session::DispatchNotifyThreadExiting(const debug_ipc::NotifyThread& notify) {
  ProcessImpl* process = system_.ProcessImplFromKoid(notify.record.process_koid);
  if (!process) {
    SendSessionNotification(SessionObserver::NotificationType::kWarning,
                            "Received thread exiting notification for an "
                            "unexpected process %" PRIu64 ".\n",
                            notify.record.process_koid);
    return;
  }

  process->OnThreadExiting(notify.record);
}

// This is the main entrypoint for all thread stops notifications in the client.
void Session::DispatchNotifyException(const debug_ipc::NotifyException& notify, bool set_metadata) {
  ThreadImpl* thread = ThreadImplFromKoid(notify.thread.process_koid, notify.thread.thread_koid);
  if (!thread) {
    SendSessionNotification(SessionObserver::NotificationType::kWarning,
                            "Received thread exception for an unknown thread.\n");
    return;
  }

  // First update the thread state so the breakpoint code can query it. This should not issue any
  // notifications.
  if (set_metadata)
    thread->SetMetadata(notify.thread);

  // The breakpoints that were hit to pass to the thread stop handler.
  StopInfo info;
  info.exception_type = notify.type;
  info.exception_record = notify.exception;

  if (!notify.hit_breakpoints.empty()) {
    // Update breakpoints' hit counts and stats. This is done before any notifications are sent so
    // that all breakpoint state is consistent.
    for (const debug_ipc::BreakpointStats& stats : notify.hit_breakpoints) {
      BreakpointImpl* impl = system_.BreakpointImplForId(stats.id);
      if (impl) {
        impl->UpdateStats(stats);
        info.hit_breakpoints.push_back(impl->GetWeakPtr());
      }
    }
  }

  // Continue if it's a conditional breakpoint.
  if (FilterConditionalBreakpoints(&info)) {
    // For simplicity, we're resuming all threads right now.
    // TODO(dangyi): It's better to continue only the affected threads.
    system_.Continue(false);
  } else {
    // This is the main notification of an exception.
    thread->OnException(info);
  }

  // Delete all one-shot breakpoints the backend deleted. This happens after the thread
  // notifications so observers can tell why the thread stopped.
  for (const auto& stats : notify.hit_breakpoints) {
    if (!stats.should_delete)
      continue;

    // Breakpoint needs deleting.
    BreakpointImpl* impl = system_.BreakpointImplForId(stats.id);
    if (impl) {
      // Need to tell the breakpoint it was removed in the backend before deleting it or it will try
      // to uninstall itself.
      impl->BackendBreakpointRemoved();
      system_.DeleteBreakpoint(impl);
    }
  }
}

void Session::DispatchNotifyModules(const debug_ipc::NotifyModules& notify) {
  ProcessImpl* process = system_.ProcessImplFromKoid(notify.process_koid);
  if (process) {
    process->OnModules(std::move(notify.modules), notify.stopped_thread_koids);
  } else {
    SendSessionNotification(SessionObserver::NotificationType::kWarning,
                            "Received modules notification for an "
                            "unexpected process %" PRIu64 ".",
                            notify.process_koid);
  }
}

void Session::DispatchProcessStarting(const debug_ipc::NotifyProcessStarting& notify) {
  if (notify.type == debug_ipc::NotifyProcessStarting::Type::kLimbo) {
    SendSessionNotification(
        SessionObserver::NotificationType::kProcessEnteredLimbo,
        fxl::StringPrintf("Process %s (%" PRIu64 ") crashed and is waiting to be attached.\n"
                          "Type \"status\" for more information.",
                          notify.name.c_str(), notify.koid));
    return;
  }

  // Search the targets to see if there is a non-attached empty one. Normally this would be the
  // initial one. Assume that targets that have a name have been set up by the user which we don't
  // want to overwrite.
  TargetImpl* found_target = nullptr;
  for (TargetImpl* target : system_.GetTargetImpls()) {
    if (target->GetState() == Target::State::kNone && target->GetArgs().empty()) {
      found_target = target;
      break;
    }
  }

  if (!found_target)  // No empty target, make a new one.
    found_target = system_.CreateNewTargetImpl(nullptr);

  if (notify.component_id == 0) {
    found_target->ProcessCreatedInJob(notify.koid, notify.name);
    return;
  }

  // We should be expecting this component.
  auto it = expected_components_.find(notify.component_id);
  FX_DCHECK(it != expected_components_.end());
  found_target->ProcessCreatedAsComponent(notify.koid, notify.name);

  // Now that the target is created, we can resume the process and make the first thread to execute.
  found_target->process()->Continue(false);
}

void Session::DispatchNotifyIO(const debug_ipc::NotifyIO& notify) {
  ProcessImpl* process = system_.ProcessImplFromKoid(notify.process_koid);
  if (!process) {
    SendSessionNotification(SessionObserver::NotificationType::kWarning,
                            "Received io notification for an unexpected process %" PRIu64 ".",
                            notify.process_koid);
    return;
  }

  FX_DCHECK(notify.type != debug_ipc::NotifyIO::Type::kLast);

  switch (notify.type) {
    case debug_ipc::NotifyIO::Type::kStdout:
    case debug_ipc::NotifyIO::Type::kStderr: {
      auto notification_type = HandleProcessIO(process, notify);
      if (notification_type != SessionObserver::NotificationType::kNone)
        SendSessionNotification(notification_type, notify.data);
      break;
    }
    case debug_ipc::NotifyIO::Type::kLast:
      FX_NOTREACHED() << "Invalid notification type";
  }
}

void Session::DispatchNotification(const debug_ipc::MsgHeader& header, std::vector<char> data) {
  debug_ipc::MessageReader reader(std::move(data));

  DEBUG_LOG(Session) << "Got notification: " << debug_ipc::MsgHeader::TypeToString(header.type);
  switch (header.type) {
    case debug_ipc::MsgHeader::Type::kNotifyProcessExiting: {
      debug_ipc::NotifyProcessExiting notify;
      if (!debug_ipc::ReadNotifyProcessExiting(&reader, &notify))
        return;

      Process* process = system_.ProcessFromKoid(notify.process_koid);
      if (process)
        process->GetTarget()->OnProcessExiting(notify.return_code);
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyProcessStarting: {
      debug_ipc::NotifyProcessStarting notify;
      if (debug_ipc::ReadNotifyProcessStarting(&reader, &notify))
        DispatchProcessStarting(notify);
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyThreadStarting:
    case debug_ipc::MsgHeader::Type::kNotifyThreadExiting: {
      debug_ipc::NotifyThread thread;
      if (!debug_ipc::ReadNotifyThread(&reader, &thread))
        break;
      if (header.type == debug_ipc::MsgHeader::Type::kNotifyThreadStarting) {
        DispatchNotifyThreadStarting(thread);
      } else {
        DispatchNotifyThreadExiting(thread);
      }
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyException: {
      debug_ipc::NotifyException notify;
      if (debug_ipc::ReadNotifyException(&reader, &notify))
        DispatchNotifyException(notify);
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyModules: {
      debug_ipc::NotifyModules notify;
      if (debug_ipc::ReadNotifyModules(&reader, &notify))
        DispatchNotifyModules(notify);
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyIO: {
      debug_ipc::NotifyIO notify;
      if (debug_ipc::ReadNotifyIO(&reader, &notify))
        DispatchNotifyIO(notify);
      break;
    }
    default:
      FX_NOTREACHED();  // Unexpected notification.
  }
}

ThreadImpl* Session::ThreadImplFromKoid(uint64_t process_koid, uint64_t thread_koid) {
  ProcessImpl* process = system_.ProcessImplFromKoid(process_koid);
  if (!process)
    return nullptr;
  return process->GetThreadImplFromKoid(thread_koid);
}

void Session::ConnectionResolved(fxl::RefPtr<PendingConnection> pending, const Err& err,
                                 const debug_ipc::HelloReply& reply,
                                 std::unique_ptr<debug_ipc::BufferedFD> buffer,
                                 fit::callback<void(const Err&)> callback) {
  if (pending.get() != pending_connection_.get()) {
    // When the connection doesn't match the pending one, that means the pending connection was
    // cancelled and we should drop the one we just got.
    if (callback)
      callback(Err(ErrType::kCanceled, "Connect operation cancelled."));
    return;
  }
  pending_connection_ = nullptr;

  if (err.has_error()) {
    // Other error connecting.
    if (callback)
      callback(err);
    return;
  }

  // Version check.
  if (reply.version != debug_ipc::kProtocolVersion) {
    if (callback) {
      callback(
          Err("ERROR: The IPC version of the debug_agent on the system (v%u) doesn't match\n"
              "the zxdb frontend's IPC version (v%u). Make sure everything is up-to-date.\n"
              "\n"
              "Closing connection.",
              reply.version, debug_ipc::kProtocolVersion));
    }
    return;
  }

  // Initialize arch-specific stuff.
  Err arch_err = SetArch(reply.arch);
  if (arch_err.has_error()) {
    if (callback)
      callback(arch_err);
    return;
  }

  // Success, connect up the stream buffers.
  connection_storage_ = std::move(buffer);

  stream_ = &connection_storage_->stream();
  connection_storage_->set_data_available_callback([this]() { OnStreamReadable(); });
  connection_storage_->set_error_callback([this]() { OnStreamError(); });

  // Issue success callbacks.
  system_.DidConnect();
  if (callback)
    callback(Err());

  // Send whatever configurations the agent should know about.
  SendAgentConfiguration();

  // Query which processes the debug agent is already connected to.
  remote_api()->Status(
      debug_ipc::StatusRequest{},
      [this, session = GetWeakPtr()](const Err& err, debug_ipc::StatusReply reply) {
        if (!session)
          return;

        if (err.has_error()) {
          SendSessionNotification(SessionObserver::NotificationType::kError,
                                  "Could not get debug agent status: %s", err.msg().c_str());
          return;
        }

        // Notify about previously connected processes.
        if (!reply.processes.empty()) {
          for (auto& observer : observers_) {
            observer.HandlePreviousConnectedProcesses(reply.processes);
          }
        }

        // Notify about processes on limbo.
        if (!reply.limbo.empty()) {
          for (auto& observer : observers_) {
            observer.HandleProcessesInLimbo(reply.limbo);
          }
        }
      });
}

void Session::AddObserver(SessionObserver* observer) { observers_.AddObserver(observer); }

void Session::RemoveObserver(SessionObserver* observer) { observers_.RemoveObserver(observer); }

void Session::AddBreakpointObserver(BreakpointObserver* observer) {
  breakpoint_observers_.AddObserver(observer);
}

void Session::RemoveBreakpointObserver(BreakpointObserver* observer) {
  breakpoint_observers_.RemoveObserver(observer);
}

void Session::AddFilterObserver(FilterObserver* observer) {
  filter_observers_.AddObserver(observer);
}

void Session::RemoveFilterObserver(FilterObserver* observer) {
  filter_observers_.RemoveObserver(observer);
}

void Session::AddDownloadObserver(DownloadObserver* observer) {
  download_observers_.AddObserver(observer);
}

void Session::RemoveDownloadObserver(DownloadObserver* observer) {
  download_observers_.RemoveObserver(observer);
}

void Session::SendSessionNotification(SessionObserver::NotificationType type, const char* fmt,
                                      ...) {
  va_list ap;
  va_start(ap, fmt);
  std::string msg = fxl::StringPrintf(fmt, ap);
  va_end(ap);

  SendSessionNotification(type, msg);
}

void Session::SendSessionNotification(SessionObserver::NotificationType type,
                                      const std::string& msg) {
  for (auto& observer : observers_)
    observer.HandleNotification(type, msg);
}

SessionObserver::NotificationType Session::HandleProcessIO(ProcessImpl* process,
                                                           const debug_ipc::NotifyIO& notify) {
  if (!process->HandleIO(notify)) {
    return SessionObserver::NotificationType::kNone;
  }

  return notify.type == debug_ipc::NotifyIO::Type::kStdout
             ? SessionObserver::NotificationType::kProcessStdout
             : SessionObserver::NotificationType::kProcessStderr;
}

void Session::ExpectComponent(uint32_t component_id) {
  FX_DCHECK(expected_components_.count(component_id) == 0);
  expected_components_.insert(component_id);
}

fxl::WeakPtr<Session> Session::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void Session::QuitAgent(fit::callback<void(const Err&)> cb) {
  bool is_connected = IsConnected();

  // We call disconnect even when is_connected is false, just in case there is a pending connection
  // going on.
  if (!is_connected) {
    Disconnect(nullptr);
    if (cb) {
      debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
        cb(Err("Not connected to a debug agent."));
      });
    }
    return;
  }

  debug_ipc::QuitAgentRequest request;
  remote_api()->QuitAgent(request, [session = GetWeakPtr(), cb = std::move(cb)](
                                       const Err& err, debug_ipc::QuitAgentReply) mutable {
    // If we received a response, there is a connection to receive it, so the session should be
    // around.
    FX_DCHECK(session && session->stream_);
    session->Disconnect(nullptr);
    if (cb)
      cb(err);
  });
}

void Session::OnSettingChanged(const SettingStore& store, const std::string& setting_name) {
  if (setting_name == ClientSettings::System::kQuitAgentOnExit) {
    SendAgentConfiguration();
  } else {
    SendSessionNotification(SessionObserver::NotificationType::kWarning,
                            "Session handling invalid setting %s", setting_name.c_str());
  }
}

void Session::ListenForSystemSettings() {
  system_.settings().AddObserver(ClientSettings::System::kQuitAgentOnExit, this);
}

void Session::SendAgentConfiguration() {
  if (!IsConnected())
    return;

  // We bungle the actions and send them in one go.
  std::vector<debug_ipc::ConfigAction> actions;
  ConfigQuitAgent(system_.settings().GetBool(ClientSettings::System::kQuitAgentOnExit), &actions);

  debug_ipc::ConfigAgentRequest request;
  request.actions = std::move(actions);
  remote_api()->ConfigAgent(request, [request, session = GetWeakPtr()](
                                         const Err& err, debug_ipc::ConfigAgentReply reply) {
    FX_DCHECK(reply.results.size() == request.actions.size());

    if (!session)
      return;

    for (size_t i = 0; i < reply.results.size(); i++) {
      debug_ipc::zx_status_t status = reply.results[i];
      if (status == debug_ipc::kZxOk)
        continue;

      auto& action = request.actions[i];
      session->SendSessionNotification(SessionObserver::NotificationType::kWarning,
                                       "Agent configuration for %s failed with result: %s",
                                       debug_ipc::ConfigAction::TypeToString(action.type),
                                       debug_ipc::ZxStatusToString(status));
    }
  });
}

void Session::ConfigQuitAgent(bool quit, std::vector<debug_ipc::ConfigAction>* actions) {
  DEBUG_LOG(RemoteAPI) << "Sending quit agent config: " << std::boolalpha << quit;
  std::string value = quit ? "true" : "false";
  actions->push_back({debug_ipc::ConfigAction::Type::kQuitOnExit, value});
}

}  // namespace zxdb
