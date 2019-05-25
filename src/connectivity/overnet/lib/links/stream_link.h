// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/links/packet_stuffer.h"
#include "src/connectivity/overnet/lib/protocol/stream_framer.h"
#include "src/connectivity/overnet/lib/routing/router.h"

namespace overnet {

class StreamLink : public Link {
 public:
  StreamLink(Router* router, NodeId peer, std::unique_ptr<StreamFramer> framer,
             uint64_t label);

  void Close(Callback<void> quiesced) override final;
  void Forward(Message message) override final;
  fuchsia::overnet::protocol::LinkStatus GetLinkStatus() override final;
  const LinkStats* GetStats() const override final { return &stats_; }

  void Process(TimeStamp received, Slice bytes);
  virtual void Emit(Slice bytes, Callback<Status> done) = 0;

  size_t maximum_segment_size() const { return framer_->maximum_segment_size; }

 private:
  void MaybeQuiesce();
  void EmitOne();
  void SetClosed();

  Router* const router_;
  std::unique_ptr<StreamFramer> framer_;
  const NodeId peer_;
  const uint64_t local_id_;
  bool emitting_ = false;
  bool closed_ = false;
  Callback<void> on_quiesced_;
  PacketStuffer packet_stuffer_;
  LinkStats stats_;
};

}  // namespace overnet
