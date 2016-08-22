// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/media/framework/parts/null_sink.h"

namespace mojo {
namespace media {

NullSink::NullSink() {}

NullSink::~NullSink() {}

PayloadAllocator* NullSink::allocator() {
  return nullptr;
}

void NullSink::SetDemandCallback(const DemandCallback& demand_callback) {
  demand_callback_ = demand_callback;
}

Demand NullSink::SupplyPacket(PacketPtr packet) {
  return Demand::kNeutral;
}

}  // namespace media
}  // namespace mojo
