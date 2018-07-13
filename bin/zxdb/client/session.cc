// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/session.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>

#include "garnet/bin/zxdb/client/arch_info.h"
#include "garnet/bin/zxdb/client/breakpoint_action.h"
#include "garnet/bin/zxdb/client/breakpoint_impl.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api_impl.h"
#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/lib/debug_ipc/client_protocol.h"
#include "garnet/lib/debug_ipc/helper/buffered_fd.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/memory/ref_counted.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Max message size before considering it corrupt. This is very large so we can
// send nontrivial memory dumps over the channel, but ensures we won't crash
// trying to allocate an unreasonable buffer size if the stream is corrupt.
constexpr uint32_t kMaxMessageSize = 16777216;

// Tries to resolve the host/port. On success populates *addr and returns Err().
Err ResolveAddress(const std::string& host, uint16_t port, addrinfo* addr) {
  std::string port_str = fxl::StringPrintf("%" PRIu16, port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_NUMERICSERV;

  struct addrinfo* addrs = nullptr;
  int addr_err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &addrs);
  if (addr_err != 0) {
    return Err(fxl::StringPrintf("Failed to resolve \"%s\": %s", host.c_str(),
                                 gai_strerror(addr_err)));
  }

  *addr = *addrs;
  freeaddrinfo(addrs);
  return Err();
}

}  // namespace

// PendingConnection -----------------------------------------------------------

// Storage for connection information when connecting dynamically. Making a
// connection has three asynchronous steps:
//
//  1. Resolving the host and connecting the socket. Since this is blocking,
//     it happens on a background thread.
//  2. Sending the hello message. Happens on the main thread.
//  3. Waiting for the reply and deserializing, then notifying the Session.
//
// Various things can happen in the middle.
//
//  - Any step can fail.
//  - The Session object can be destroyed (weak pointer checks).
//  - The connection could be canceled by the user (the session callback
//    checks for this).
class Session::PendingConnection
    : public fxl::RefCountedThreadSafe<PendingConnection> {
 public:
  void Initiate(fxl::WeakPtr<Session> session,
                std::function<void(const Err&)> callback);

  // There are no other functions since this will be running on a background
  // thread and the class state can't be safely retrieved. It reports all of
  // the output state via ConnectionResolved.

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(PendingConnection);
  FRIEND_MAKE_REF_COUNTED(PendingConnection);

  PendingConnection(const std::string& host, uint16_t port)
      : host_(host), port_(port) {}
  ~PendingConnection() {}

  // These are the steps of connection, in order. They each take a RefPtr
  // to |this| to ensure the class is in scope for the full flow.
  void ConnectBackgroundThread(fxl::RefPtr<PendingConnection> owner);
  void ConnectCompleteMainThread(fxl::RefPtr<PendingConnection> owner,
                                 const Err& err);
  void DataAvailableMainThread(fxl::RefPtr<PendingConnection> owner);
  void HelloCompleteMainThread(fxl::RefPtr<PendingConnection> owner,
                               const Err& err,
                               const debug_ipc::HelloReply& reply);

  // Creates the connection (called on the background thread). On success
  // the socket_ is populated.
  Err DoConnectBackgroundThread();

  std::string host_;
  uint16_t port_;

  // Only non-null when in the process of connecting.
  std::unique_ptr<std::thread> thread_;

  debug_ipc::MessageLoop* main_loop_ = nullptr;

  // Access only on the main thread.
  fxl::WeakPtr<Session> session_;

  // The constructed socket and buffer.
  //
  // The socket is created by ConnectBackgroundThread and read by
  // HelloCompleteMainThread to create the buffer so needs no synchronization.
  // It would be cleaner to pass this in the lambdas to avoid threading
  // confusion, but move-only types can't be bound.
  fxl::UniqueFD socket_;
  std::unique_ptr<debug_ipc::BufferedFD> buffer_;

  // Callback when the conncetion is complete (or fails). Access only on the
  // main thread.
  std::function<void(const Err&)> callback_;
};

void Session::PendingConnection::Initiate(
    fxl::WeakPtr<Session> session, std::function<void(const Err&)> callback) {
  FXL_DCHECK(!thread_.get());  // Duplicate Initiate() call.

  main_loop_ = debug_ipc::MessageLoop::Current();
  session_ = std::move(session);
  callback_ = std::move(callback);

  // Create the background thread, and run the background function. The
  // context will keep a ref to this class.
  thread_ =
      std::make_unique<std::thread>([owner = fxl::RefPtr<PendingConnection>(
                                         this)]() {
        owner->ConnectBackgroundThread(owner);
      });
}

void Session::PendingConnection::ConnectBackgroundThread(
    fxl::RefPtr<PendingConnection> owner) {
  Err err = DoConnectBackgroundThread();
  main_loop_->PostTask([ owner = std::move(owner), err ]() {
    owner->ConnectCompleteMainThread(owner, err);
  });
}

void Session::PendingConnection::ConnectCompleteMainThread(
    fxl::RefPtr<PendingConnection> owner, const Err& err) {
  // The background thread function has now completed so the thread can be
  // destroyed. We do want to join with the thread here to ensure there are no
  // references to the PendingConnection on the background thread, which might
  // in turn cause the PendingConnection to be destroyed on the background
  // thread.
  thread_->join();
  thread_.reset();

  if (!session_ || err.has_error()) {
    // Error or session destroyed, skip sending hello and forward the error.
    HelloCompleteMainThread(owner, err, debug_ipc::HelloReply());
    return;
  }

  FXL_DCHECK(socket_.is_valid());
  buffer_ = std::make_unique<debug_ipc::BufferedFD>();
  buffer_->Init(std::move(socket_));

  // Send "Hello" message. We can't use the Session::Send infrastructure
  // since the connection hasn't technically been established yet.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteRequest(debug_ipc::HelloRequest(), 1, &writer);
  std::vector<char> serialized = writer.MessageComplete();
  buffer_->stream().Write(std::move(serialized));

  buffer_->set_data_available_callback(
      [owner]() { owner->DataAvailableMainThread(owner); });
  buffer_->set_error_callback([owner]() {
    owner->HelloCompleteMainThread(owner, Err("Connection error."),
                                   debug_ipc::HelloReply());
  });
}

void Session::PendingConnection::DataAvailableMainThread(
    fxl::RefPtr<PendingConnection> owner) {
  // This function needs to manually deserialize the hello message since
  // the Session stuff isn't connected yet.
  constexpr size_t kHelloMessageSize =
      debug_ipc::MsgHeader::kSerializedHeaderSize +
      sizeof(debug_ipc::HelloReply);

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

void Session::PendingConnection::HelloCompleteMainThread(
    fxl::RefPtr<PendingConnection> owner, const Err& err,
    const debug_ipc::HelloReply& reply) {
  // Prevent future notifications.
  if (buffer_.get()) {
    buffer_->set_data_available_callback(std::function<void()>());
    buffer_->set_error_callback(std::function<void()>());
  }

  if (session_) {
    // The buffer must be created here on the main thread since it will
    // register with the message loop to watch the FD.

    // If the session exists, always tell it about the completion, whether
    // the connection was successful or not. It will issue the callback.
    session_->ConnectionResolved(std::move(owner), err, reply,
                                 std::move(buffer_), std::move(callback_));
  } else if (callback_) {
    // Session was destroyed. Issue the callback with an error (not clobbering
    // an existing one if there was one).
    if (err.has_error())
      callback_(err);
    else
      callback_(Err("Session was destroyed."));
  }
}

Err Session::PendingConnection::DoConnectBackgroundThread() {
  addrinfo addr;
  Err err = ResolveAddress(host_, port_, &addr);
  if (err.has_error())
    return err;

  socket_.reset(socket(AF_INET, SOCK_STREAM, 0));
  if (!socket_.is_valid())
    return Err(ErrType::kGeneral, "Could not create socket.");

  if (connect(socket_.get(), addr.ai_addr, addr.ai_addrlen)) {
    socket_.reset();
    return Err(
        fxl::StringPrintf("Failed to connect socket: %s", strerror(errno)));
  }

  // By default sockets are blocking which we don't want.
  if (fcntl(socket_.get(), F_SETFL, O_NONBLOCK) < 0) {
    socket_.reset();
    return Err(ErrType::kGeneral, "Could not make nonblocking socket.");
  }

  return Err();
}

// Session ---------------------------------------------------------------------

Session::Session()
    : remote_api_(std::make_unique<RemoteAPIImpl>(this)),
      system_(this),
      weak_factory_(this) {}

Session::Session(std::unique_ptr<RemoteAPI> remote_api)
    : remote_api_(std::move(remote_api)), system_(this), weak_factory_(this) {}

Session::Session(debug_ipc::StreamBuffer* stream)
    : stream_(stream), system_(this), weak_factory_(this) {}

Session::~Session() = default;

void Session::OnStreamReadable() {
  if (!stream_)
    return;  // Notification could have raced with detaching the stream.

  while (true) {
    if (!stream_->IsAvailable(debug_ipc::MsgHeader::kSerializedHeaderSize))
      return;

    std::vector<char> serialized_header;
    serialized_header.resize(debug_ipc::MsgHeader::kSerializedHeaderSize);
    stream_->Peek(&serialized_header[0],
                  debug_ipc::MsgHeader::kSerializedHeaderSize);

    debug_ipc::MessageReader reader(std::move(serialized_header));
    debug_ipc::MsgHeader header;
    if (!reader.ReadHeader(&header)) {
      // Since we already validated there is enough data for the header, the
      // header read should not fail (it's just a memcpy).
      FXL_NOTREACHED();
      return;
    }

    // Sanity checking on the size to prevent crashes.
    if (header.size > kMaxMessageSize) {
      fprintf(stderr,
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

    // Consume the message now that we know the size. Do this before doing
    // anything else so the data is consumed if the size is right, even if the
    // transaction ID is wrong.
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
      fprintf(stderr,
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
  ClearConnectionData();
  // TODO(brettw) DX-301 issue some kind of notification and mark all processes
  // as terminated.
}

bool Session::IsConnected() const { return !!stream_; }

void Session::Connect(const std::string& host, uint16_t port,
                      std::function<void(const Err&)> callback) {
  Err err;
  if (IsConnected()) {
    err = Err("Already connected.");
  } else if (pending_connection_.get()) {
    err = Err("A connection is already pending.");
  }

  if (err.has_error()) {
    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask(
          [callback, err]() { callback(err); });
    }
    return;
  }

  pending_connection_ = fxl::MakeRefCounted<PendingConnection>(host, port);
  pending_connection_->Initiate(weak_factory_.GetWeakPtr(),
                                std::move(callback));
}

void Session::Disconnect(std::function<void(const Err&)> callback) {
  if (!IsConnected()) {
    Err err;
    if (pending_connection_.get()) {
      // Cancel pending connection.
      pending_connection_ = nullptr;
    } else {
      err = Err("Not connected.");
    }

    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask(
          [callback, err]() { callback(err); });
    }
    return;
  }

  if (!connection_storage_) {
    // The connection is persistent (passed in via the constructor) and can't
    // be disconnected.
    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask([callback]() {
        callback(Err(ErrType::kGeneral,
                     "The connection can't be disconnected in this build "
                     "of the debugger."));
      });
      return;
    }
  }

  ClearConnectionData();

  if (callback) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback]() { callback(Err()); });
  }
}

void Session::ClearConnectionData() {
  stream_ = nullptr;
  connection_storage_.reset();
  arch_info_.reset();
  arch_ = debug_ipc::Arch::kUnknown;
}

void Session::DispatchNotifyThread(debug_ipc::MsgHeader::Type type,
                                   const debug_ipc::NotifyThread& notify) {
  ProcessImpl* process = system_.ProcessImplFromKoid(notify.process_koid);
  if (process) {
    if (type == debug_ipc::MsgHeader::Type::kNotifyThreadStarting)
      process->OnThreadStarting(notify.record);
    else
      process->OnThreadExiting(notify.record);
  } else {
    fprintf(stderr,
            "Warning: received thread notification for an "
            "unexpected process %" PRIu64 ".",
            notify.process_koid);
  }
}

// This is the main entrypoint for all thread stops notifications in the client.
void Session::DispatchNotifyException(
    const debug_ipc::NotifyException& notify) {
  ThreadImpl* thread =
      ThreadImplFromKoid(notify.process_koid, notify.thread.koid);
  if (!thread) {
    fprintf(stderr,
            "Warning: received thread exception for an unknown thread.\n");
    return;
  }

  // First update the thread state so the breakpoint code can query it.
  // This should not issue any notifications.
  thread->SetMetadataFromException(notify);

  // The breakpoints that were hit to pass to the thread stop handler.
  std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints;

  if (!notify.hit_breakpoints.empty()) {
    // Update breakpoints' hit counts and stats. This is done in a separate
    // phase before notifying the breakpoints of the action so all breakpoints'
    // state is consistent since it's possible to write a breakpoint handler
    // that queries other breakpoints statistics.
    for (const debug_ipc::BreakpointStats& stats : notify.hit_breakpoints) {
      BreakpointImpl* impl = system_.BreakpointImplForId(stats.breakpoint_id);
      if (impl)
        impl->UpdateStats(stats);
    }

    // Give any hit breakpoints a say in what happens when they're hit. The
    // initial value of "action" should be the lowest precedence action.
    //
    // Watch out: a breakpoint handler could do anything, including deleting
    // other breakpoints. This re-queries the breakpoints by ID in the loop
    // in case that happens.
    BreakpointAction action = BreakpointAction::kContinue;
    for (const debug_ipc::BreakpointStats& stats : notify.hit_breakpoints) {
      BreakpointImpl* impl = system_.BreakpointImplForId(stats.breakpoint_id);
      if (!impl)
        continue;

      BreakpointAction new_action = impl->OnHit(thread);
      if (new_action == BreakpointAction::kStop && !impl->is_internal())
        hit_breakpoints.push_back(impl->GetWeakPtr());
      action = BreakpointActionHighestPrecedence(action, new_action);
    }

    switch (action) {
      case BreakpointAction::kContinue:
        thread->Continue();
        return;
      case BreakpointAction::kSilentStop:
        // Do nothing when a silent stop is requested.
        return;
      case BreakpointAction::kStop:
        // Fall through to normal thread stop handling.
        break;
    }
    // Fall through to normal thread stop handling.
  }

  thread->DispatchExceptionNotification(notify.type, hit_breakpoints);

  // Delete all one-shot breakpoints the backend deleted. This happens after
  // the thread notifications so observers can tell why the thread stopped.
  for (const auto& stats : notify.hit_breakpoints) {
    if (!stats.should_delete)
      continue;

    // Breakpoint needs deleting.
    BreakpointImpl* impl = system_.BreakpointImplForId(stats.breakpoint_id);
    if (impl) {
      // Need to tell the breakpoint it was removed in the backend before
      // deleting it or it will try to uninstall itself.
      impl->BackendBreakpointRemoved();
      system_.DeleteBreakpoint(impl);
    }
  }
}

void Session::DispatchNotifyModules(const debug_ipc::NotifyModules& notify) {
  ProcessImpl* process = system_.ProcessImplFromKoid(notify.process_koid);
  if (process) {
    process->OnModules(notify.modules, notify.stopped_thread_koids);
  } else {
    fprintf(stderr,
            "Warning: received modules notification for an "
            "unexpected process %" PRIu64 ".",
            notify.process_koid);
  }
}

void Session::DispatchNotification(const debug_ipc::MsgHeader& header,
                                   std::vector<char> data) {
  debug_ipc::MessageReader reader(std::move(data));

  switch (header.type) {
    case debug_ipc::MsgHeader::Type::kNotifyProcessExiting: {
      debug_ipc::NotifyProcess notify;
      if (!debug_ipc::ReadNotifyProcess(&reader, &notify))
        return;

      Process* process = system_.ProcessFromKoid(notify.process_koid);
      if (process)
        process->GetTarget()->OnProcessExiting(notify.return_code);
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyThreadStarting:
    case debug_ipc::MsgHeader::Type::kNotifyThreadExiting: {
      debug_ipc::NotifyThread thread;
      if (debug_ipc::ReadNotifyThread(&reader, &thread))
        DispatchNotifyThread(header.type, thread);
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
    default:
      FXL_NOTREACHED();  // Unexpected notification.
  }
}

ThreadImpl* Session::ThreadImplFromKoid(uint64_t process_koid,
                                        uint64_t thread_koid) {
  ProcessImpl* process = system_.ProcessImplFromKoid(process_koid);
  if (!process)
    return nullptr;
  return process->GetThreadImplFromKoid(thread_koid);
}

void Session::ConnectionResolved(fxl::RefPtr<PendingConnection> pending,
                                 const Err& err,
                                 const debug_ipc::HelloReply& reply,
                                 std::unique_ptr<debug_ipc::BufferedFD> buffer,
                                 std::function<void(const Err&)> callback) {
  if (pending.get() != pending_connection_.get()) {
    // When the connection doesn't match the pending one, that means the
    // pending connection was cancelled and we should drop the one we just
    // got.
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
  if (reply.version != debug_ipc::HelloReply::kCurrentVersion) {
    if (callback) {
      callback(Err(fxl::StringPrintf(
          "Protocol version mismatch. The target system debug agent reports "
          "version %" PRIu32 " but this client expects version %" PRIu32 ".",
          reply.version, debug_ipc::HelloReply::kCurrentVersion)));
    }
  }

  // Initialize arch-specific stuff.
  arch_info_ = std::make_unique<ArchInfo>();
  Err arch_err = arch_info_->Init(reply.arch);
  if (arch_err.has_error()) {
    if (callback)
      callback(arch_err);
    return;
  }

  // Success, connect up the stream buffers.
  connection_storage_ = std::move(buffer);

  arch_ = reply.arch;
  stream_ = &connection_storage_->stream();
  connection_storage_->set_data_available_callback(
      [this]() { OnStreamReadable(); });
  connection_storage_->set_error_callback([this]() { OnStreamError(); });

  // Issue success callbacks.
  system_.DidConnect();
  if (callback)
    callback(Err());
}

}  // namespace zxdb
