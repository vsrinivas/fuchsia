// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/routing/router.h"

namespace overnet {

class StreamLink : public Link {
 public:
  StreamLink(Router* router, NodeId peer, uint32_t mss, uint64_t label);

  void Close(Callback<void> quiesced) override final;
  void Forward(Message message) override final;
  fuchsia::overnet::protocol::LinkStatus GetLinkStatus() override final;
  const LinkStats* GetStats() const override final { return &stats_; }

  void Process(TimeStamp received, Slice bytes);
  virtual void Emit(Slice bytes, Callback<Status> done) = 0;

 private:
  void MaybeQuiesce();

  const size_t mss_;
  Router* const router_;
  const NodeId peer_;
  const uint64_t local_id_;
  bool emitting_ = false;
  bool closed_ = false;
  Slice buffered_input_;
  Callback<void> on_quiesced_;
  LinkStats stats_;
};

}  // namespace overnet
