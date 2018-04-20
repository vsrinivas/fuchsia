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

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/lib/debug_ipc/helper/buffered_fd.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "garnet/public/lib/fxl/logging.h"
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
    return Err(fxl::StringPrintf(
        "Failed to resolve \"%s\": %s", host.c_str(), gai_strerror(addr_err)));
  }

  *addr = *addrs;
  freeaddrinfo(addrs);
  return Err();
}

}  // namespace

// Storage for connection information when connecting dynamically.
struct Session::ConnectionStorage {
  debug_ipc::BufferedFD buffer;
};

Session::Session() : system_(this) {}

Session::Session(debug_ipc::StreamBuffer* stream)
    : stream_(stream), system_(this) {}

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

bool Session::IsConnected() const {
  return !!stream_;
}

void Session::Connect(const std::string& host,
                      uint16_t port,
                      std::function<void(const Err&)> callback) {
  Err err = MakeConnection(host, port);
  if (err.has_error()) {
    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask([callback, err]() {
        callback(err);
      });
    }
    return;
  }

  stream_ = &connection_storage_->buffer.stream();
  connection_storage_->buffer.set_data_available_callback(
      [this](){ OnStreamReadable(); });

  // TODO(brettw) testing.
  Send<debug_ipc::HelloRequest, debug_ipc::HelloReply>(
      debug_ipc::HelloRequest(),
      [callback](const Err& err, debug_ipc::HelloReply reply) {
        if (callback)
          callback(err);
      });
}

void Session::Disconnect(std::function<void(const Err&)> callback) {
  if (!IsConnected()) {
    // No connection to disconnect.
    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask(
          [callback]() { callback(Err(ErrType::kGeneral, "Not connected.")); });
      return;
    }
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

  stream_ = nullptr;
  connection_storage_.reset();
  if (callback) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback]() { callback(Err()); });
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
      if (!debug_ipc::ReadNotifyThread(&reader, &thread))
        return;

      ProcessImpl* process = system_.ProcessImplFromKoid(thread.process_koid);
      if (process) {
        if (header.type == debug_ipc::MsgHeader::Type::kNotifyThreadStarting)
          process->OnThreadStarting(thread.record);
        else
          process->OnThreadExiting(thread.record);
      } else {
        fprintf(stderr,
                "Warning: received thread notification for an "
                "unexpected process %" PRIu64 ".",
                thread.process_koid);
      }
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyException: {
      debug_ipc::NotifyException notify;
      if (!debug_ipc::ReadNotifyException(&reader, &notify))
        return;
      ThreadImpl* thread =
          ThreadImplFromKoid(notify.process_koid, notify.thread.koid);
      if (thread)
        thread->OnException(notify);
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

Err Session::MakeConnection(const std::string& host, uint16_t port) {
  if (IsConnected())
    return Err(ErrType::kGeneral, "Already connected.");

  addrinfo addr;
  Err err = ResolveAddress(host, port, &addr);
  if (err.has_error())
    return err;

  auto storage = std::make_unique<ConnectionStorage>();

  // Create socket.
  fxl::UniqueFD sock(socket(AF_INET, SOCK_STREAM, 0));
  if (!sock.is_valid())
    return Err(ErrType::kGeneral, "Could not create socket.");

  // Connect.
  if (connect(sock.get(), addr.ai_addr, addr.ai_addrlen)) {
    return Err(fxl::StringPrintf("Failed to connect socket: %s",
                                 strerror(errno)));
  }

  if (fcntl(sock.get(), F_SETFL, O_NONBLOCK) < 0)
    return Err(ErrType::kGeneral, "Could not make nonblocking socket.");

  storage->buffer.Init(std::move(sock));

  connection_storage_ = std::move(storage);
  return Err();
}

}  // namespace zxdb
