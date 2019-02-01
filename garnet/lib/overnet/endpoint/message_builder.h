// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/overnet/endpoint/router_endpoint.h"

namespace overnet {

// Messages contain:
// - a prelude, indicating which message type is contained
// - text, usually FIDL bytes representing the content of a message
// - proxied handles: on fuchsia, these are zircon handles that overnet
//   maintains a proxy for
//
// Utilities in this file assist in translating between these rich messages
// and overnet message bodies (which are simply slices).

// Target for building an outgoing message (to the network).
class MessageSender {
 public:
  virtual Status SetTransactionId(uint32_t txid) = 0;
  virtual Status SetOrdinal(uint32_t ordinal) = 0;
  virtual Status SetBody(Slice body) = 0;
  virtual Status AppendUnknownHandle() = 0;
  virtual StatusOr<RouterEndpoint::NewStream> AppendChannelHandle(
      fuchsia::overnet::protocol::Introduction introduction) = 0;
};

// Target for building an incoming message (from the network).
class MessageReceiver {
 public:
  virtual Status SetTransactionId(uint32_t txid) = 0;
  virtual Status SetOrdinal(uint32_t ordinal) = 0;
  virtual Status SetBody(Slice body) = 0;
  virtual Status AppendUnknownHandle() = 0;
  virtual Status AppendChannelHandle(
      RouterEndpoint::ReceivedIntroduction stream) = 0;
};

// Concrete implementation of a MessageSender that creates a Slice that can be
// interpreted by ParseMessageInto.
class MessageWireEncoder final : public MessageSender {
 public:
  MessageWireEncoder(RouterEndpoint::Stream* stream) : stream_(stream) {}

  Status SetTransactionId(uint32_t txid) override {
    txid_ = txid;
    return Status::Ok();
  }
  Status SetOrdinal(uint32_t ordinal) override {
    ordinal_ = ordinal;
    return Status::Ok();
  }
  Status SetBody(Slice body) override {
    body_ = std::move(body);
    return Status::Ok();
  }
  Status AppendUnknownHandle() override {
    return Status(StatusCode::FAILED_PRECONDITION,
                  "Unknown handle types not supported for encoding");
  }
  StatusOr<RouterEndpoint::NewStream> AppendChannelHandle(
      fuchsia::overnet::protocol::Introduction introduction) override;

  Slice Write(Border desired_border) const;

 private:
  RouterEndpoint::Stream* const stream_;

  uint32_t txid_ = 0;
  uint32_t ordinal_ = 0;
  Slice body_;
  std::vector<Slice> tail_;
};

Status ParseMessageInto(Slice slice, NodeId peer,
                        RouterEndpoint* router_endpoint,
                        MessageReceiver* builder);

}  // namespace overnet
