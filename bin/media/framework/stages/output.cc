// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/output.h"

#include "apps/media/src/framework/engine.h"
#include "apps/media/src/framework/stages/stage.h"

namespace media {

Output::Output(Stage* stage, size_t index) : stage_(stage), index_(index) {}

Output::~Output() {}

void Output::Connect(Input* input) {
  FTL_DCHECK(input);
  FTL_DCHECK(!mate_);
  mate_ = input;
}

void Output::SetCopyAllocator(PayloadAllocator* copy_allocator) {
  FTL_DCHECK(connected());
  copy_allocator_ = copy_allocator;
}

Demand Output::demand() const {
  FTL_DCHECK(mate_);

  // Return negative demand if mate() already has a packet.
  // We check demand_ here to possibly avoid the second check.
  if (demand_ == Demand::kNegative || mate_->packet_from_upstream()) {
    return Demand::kNegative;
  }

  return demand_;
}

void Output::SupplyPacket(PacketPtr packet) const {
  FTL_DCHECK(packet);
  FTL_DCHECK(mate_);

  if (copy_allocator_ != nullptr) {
    // Need to copy the packet due to an allocation conflict.
    size_t size = packet->size();
    void* buffer;

    if (size == 0) {
      buffer = nullptr;
    } else {
      buffer = copy_allocator_->AllocatePayloadBuffer(size);
      if (buffer == nullptr) {
        FTL_LOG(WARNING) << "allocator starved copying output";
        return;
      }
      memcpy(buffer, packet->payload(), size);
    }

    packet =
        Packet::Create(packet->pts(), packet->pts_rate(), packet->keyframe(),
                       packet->end_of_stream(), size, buffer, copy_allocator_);
  }

  if (mate_->SupplyPacketFromOutput(std::move(packet))) {
    stage_->engine()->PushToSupplyBacklog(mate_->stage());
  }
}

bool Output::UpdateDemandFromInput(Demand demand) {
  if (demand_ == demand) {
    return false;
  }
  demand_ = demand;
  return true;
}

void Output::Flush() {
  demand_ = Demand::kNegative;
}

}  // namespace media
