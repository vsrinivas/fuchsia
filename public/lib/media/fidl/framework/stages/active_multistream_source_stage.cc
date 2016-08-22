// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/media/framework/stages/active_multistream_source_stage.h"
#include "services/media/framework/stages/util.h"

namespace mojo {
namespace media {

ActiveMultistreamSourceStage::ActiveMultistreamSourceStage(
    std::shared_ptr<ActiveMultistreamSource> source)
    : outputs_(source->stream_count()),
      packets_per_output_(source->stream_count()),
      source_(source) {
  DCHECK(source);

  supply_function_ = [this](size_t output_index, PacketPtr packet) {
    lock_.Acquire();
    DCHECK(output_index < outputs_.size());
    DCHECK(outputs_.size() == packets_per_output_.size());
    DCHECK(packet);
    DCHECK(packet_request_outstanding_);

    packet_request_outstanding_ = false;

    if (packet->end_of_stream()) {
      ++ended_streams_;
    }

    // We put new packets in per-output (per-stream) queues. That way, when
    // we get a bunch of undemanded packets for a particular stream, we can
    // queue them up here until they're demanded.
    std::deque<PacketPtr>& packets = packets_per_output_[output_index];
    packets.push_back(std::move(packet));

    if (packets.size() == 1 &&
        outputs_[output_index].demand() != Demand::kNegative) {
      // We have a packet for an output with non-negative demand that didn't
      // have one before. Request an update. Update will request another
      // packet, if needed.
      lock_.Release();
      RequestUpdate();
    } else {
      // We got a packet, but it doesn't change matters, either because the
      // output in question already had a packet queued or because that output
      // has negative demand and wasn't the one we wanted a packet for.
      // We can request another packet without having to go through an update.
      source_->RequestPacket();
      packet_request_outstanding_ = true;
      lock_.Release();
    }
  };

  source_->SetSupplyCallback(supply_function_);
}

ActiveMultistreamSourceStage::~ActiveMultistreamSourceStage() {}

size_t ActiveMultistreamSourceStage::input_count() const {
  return 0;
};

Input& ActiveMultistreamSourceStage::input(size_t index) {
  CHECK(false) << "input requested from source";
  abort();
}

size_t ActiveMultistreamSourceStage::output_count() const {
  return outputs_.size();
}

Output& ActiveMultistreamSourceStage::output(size_t index) {
  DCHECK(index < outputs_.size());
  return outputs_[index];
}

PayloadAllocator* ActiveMultistreamSourceStage::PrepareInput(size_t index) {
  CHECK(false) << "PrepareInput called on source";
  return nullptr;
}

void ActiveMultistreamSourceStage::PrepareOutput(
    size_t index,
    PayloadAllocator* allocator,
    const UpstreamCallback& callback) {
  DCHECK(index < outputs_.size());

  if (allocator != nullptr) {
    // Currently, we don't support a source that uses provided allocators. If
    // we're provided an allocator, the output must have it so supplied packets
    // can be copied.
    outputs_[index].SetCopyAllocator(allocator);
  }
}

void ActiveMultistreamSourceStage::UnprepareOutput(
    size_t index,
    const UpstreamCallback& callback) {
  DCHECK(index < outputs_.size());
  outputs_[index].SetCopyAllocator(nullptr);
}

void ActiveMultistreamSourceStage::Update(Engine* engine) {
  base::AutoLock lock(lock_);
  DCHECK(engine);

  DCHECK(outputs_.size() == packets_per_output_.size());

  bool need_packet = false;

  for (size_t i = 0; i < outputs_.size(); i++) {
    Output& output = outputs_[i];
    std::deque<PacketPtr>& packets = packets_per_output_[i];

    if (packets.empty()) {
      if (output.demand() == Demand::kPositive) {
        // The output has positive demand and no packets queued. Request another
        // packet so we can meet the demand.
        need_packet = true;
      }
    } else if (output.demand() != Demand::kNegative) {
      // The output has non-negative demand and packets queued. Send a packet
      // downstream now.
      output.SupplyPacket(std::move(packets.front()), engine);
      packets.pop_front();
    }
  }

  if (need_packet && !packet_request_outstanding_) {
    source_->RequestPacket();
    packet_request_outstanding_ = true;
  }
}

void ActiveMultistreamSourceStage::FlushInput(
    size_t index,
    const DownstreamCallback& callback) {
  CHECK(false) << "FlushInput called on source";
}

void ActiveMultistreamSourceStage::FlushOutput(size_t index) {
  base::AutoLock lock(lock_);
  DCHECK(index < outputs_.size());
  DCHECK(source_);
  outputs_[index].Flush();
  packets_per_output_[index].clear();
  ended_streams_ = 0;
  packet_request_outstanding_ = false;
}

}  // namespace media
}  // namespace mojo
