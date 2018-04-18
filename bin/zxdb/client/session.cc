// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/session.h"

#include <arpa/inet.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/socket.h>

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

}  // namespace

Session::Session() : system_(this) {}

Session::Session(debug_ipc::StreamBuffer* stream)
    : stream_(stream), system_(this) {}

Session::~Session() = default;

void Session::OnStreamReadable() {
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
              "Bad message received of size %u.\n(type = %u, transaction = %u)\n",
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

void Session::Connect(const std::string& host, uint16_t port,
                      std::function<void(const Err&)> callback) {
  // TODO(brettw) implement this.
}

void Session::Disconnect(std::function<void(const Err&)> callback) {
  // TODO(brettw) implement this.
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
        fprintf(stderr, "Warning: received thread notification for an "
                "unexpected process %" PRIu64 ".", thread.process_koid);
      }
      break;
    }
    case debug_ipc::MsgHeader::Type::kNotifyException: {
      debug_ipc::NotifyException notify;
      if (!debug_ipc::ReadNotifyException(&reader, &notify))
        return;
      ThreadImpl* thread = ThreadImplFromKoid(
          notify.process_koid, notify.thread.koid);
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

}  // namespace zxdb
