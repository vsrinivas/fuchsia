// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/session.h"

#include <stdio.h>

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
Session::~Session() = default;

void Session::SetAgentConnection(std::unique_ptr<AgentConnection> connection) {
  // Cancel out all pending connections. Be careful with the ordering in case
  // the callbacks initiate new requests.
  std::map<uint32_t, Callback> old_pending = std::move(pending_);
  agent_connection_ = std::move(connection);

  if (!old_pending.empty()) {
    Err err(ErrType::kNoConnection, "Connection lost.");
    for (const auto& pair : old_pending)
      pair.second(this, pair.first, err, std::vector<char>());
  }
}

void Session::OnAgentData(debug_ipc::StreamBuffer* stream) {
  if (!stream->IsAvailable(debug_ipc::MsgHeader::kSerializedHeaderSize))
    return;

  std::vector<char> serialized_header;
  serialized_header.resize(debug_ipc::MsgHeader::kSerializedHeaderSize);
  stream->Peek(&serialized_header[0],
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

  if (!stream->IsAvailable(header.size))
    return;  // Wait for more data.

  // Consume the message now that we know the size. Do this before doing
  // anything else so the data is consumed if the size is right, even if the
  // transaction ID is wrong.
  std::vector<char> serialized;
  serialized.resize(header.size);
  stream->Read(&serialized[0], header.size);

  // Find the transaction.
  auto found = pending_.find(header.transaction_id);
  if (found == pending_.end()) {
    fprintf(stderr,
            "Received reply for unexpected transaction %u (type = %u).\n",
            static_cast<unsigned>(header.transaction_id),
            static_cast<unsigned>(header.type));
    // Just ignore this bad message.
    return;
  }

  // Do the type-specific deserialization and callback.
  found->second(this, header.transaction_id, Err(), std::move(serialized));

  pending_.erase(found);
}

}  // namespace zxdb
