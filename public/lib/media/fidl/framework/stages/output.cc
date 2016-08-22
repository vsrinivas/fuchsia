// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/media/framework/engine.h"
#include "services/media/framework/stages/output.h"
#include "services/media/framework/stages/stage.h"

namespace mojo {
namespace media {

Output::Output() : demand_(Demand::kNegative), copy_allocator_(nullptr) {}

Output::~Output() {}

void Output::Connect(const InputRef& input) {
  DCHECK(input.valid());
  DCHECK(!mate_);
  mate_ = input;
}

Input& Output::actual_mate() const {
  DCHECK(mate_.valid());
  return mate_.actual();
}

void Output::SetCopyAllocator(PayloadAllocator* copy_allocator) {
  DCHECK(connected());
  copy_allocator_ = copy_allocator;
}

Demand Output::demand() const {
  DCHECK(connected());

  // Return negative demand if mate() already has a packet.
  // We check demand_ here to possibly avoid the second check.
  if (demand_ == Demand::kNegative || actual_mate().packet_from_upstream()) {
    return Demand::kNegative;
  }

  return demand_;
}

void Output::SupplyPacket(PacketPtr packet, Engine* engine) const {
  DCHECK(packet);
  DCHECK(engine);
  DCHECK(connected());

  if (copy_allocator_ != nullptr) {
    // Need to copy the packet due to an allocation conflict.
    size_t size = packet->size();
    void* buffer;

    if (size == 0) {
      buffer = nullptr;
    } else {
      buffer = copy_allocator_->AllocatePayloadBuffer(size);
      if (buffer == nullptr) {
        LOG(WARNING) << "allocator starved copying output";
        return;
      }
      memcpy(buffer, packet->payload(), size);
    }

    packet = Packet::Create(packet->pts(), packet->end_of_stream(), size,
                            buffer, copy_allocator_);
  }

  if (actual_mate().SupplyPacketFromOutput(std::move(packet))) {
    engine->PushToSupplyBacklog(mate_.stage_);
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
}  // namespace mojo
