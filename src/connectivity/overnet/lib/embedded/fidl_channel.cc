// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/fidl_channel.h"

namespace overnet {
namespace internal {

FidlChannel::FidlChannel(ClosedPtr<ZxChannel> channel)
    : io_(fbl::MakeRefCounted<FidlChannelIO>(this, std::move(channel))) {}

FidlChannel::~FidlChannel() { io_->Orphan(); }

ClosedPtr<ZxChannel> FidlChannel::TakeChannel_() {
  ZX_ASSERT(io_.get());
  auto channel = io_->TakeChannel();
  ZX_ASSERT(channel);
  io_.reset();
  return channel;
}

FidlChannelIO::FidlChannelIO(FidlChannel* parent, ClosedPtr<ZxChannel> channel)
    : parent_(parent), channel_(std::move(channel)) {
  channel_->SetReader(this);
}

FidlChannelIO::~FidlChannelIO() { channel_->SetReader(nullptr); }

void FidlChannelIO::Send(
    const fidl_type_t* type,
    fuchsia::overnet::protocol::ZirconChannelMessage message,
    fit::function<zx_status_t(fuchsia::overnet::protocol::ZirconChannelMessage)>
        callback) {
  while (next_txid_ == 0 || pending_.count(next_txid_)) {
    next_txid_ = (next_txid_ + 1) & FIDL_ORD_SYSTEM_MASK;
  }
  auto txid = next_txid_++;
  reinterpret_cast<fidl_message_header_t*>(message.bytes.data())->txid = txid;
  pending_.emplace(txid, std::move(callback));
  Send(type, std::move(message));
}

void FidlChannelIO::Send(
    const fidl_type_t* type,
    fuchsia::overnet::protocol::ZirconChannelMessage message) {
  channel_->Message(std::move(message));
}

ClosedPtr<ZxChannel> FidlChannelIO::TakeChannel() {
  ZX_ASSERT(pending_.empty());
  return std::move(channel_);
}

void FidlChannelIO::Message(
    fuchsia::overnet::protocol::ZirconChannelMessage message) {
  fidl_message_header_t header;
  if (message.bytes.size() < sizeof(header)) {
    return;
  }
  memcpy(&header, message.bytes.data(), sizeof(header));

  if (header.txid != 0) {
    if (auto it = pending_.find(header.txid); it != pending_.end()) {
      auto cb = std::move(it->second);
      pending_.erase(it);
      cb(std::move(message));
      return;
    }
  }

  if (parent_ != nullptr) {
    parent_->Dispatch_(std::move(message));
  }
}

}  // namespace internal
}  // namespace overnet
