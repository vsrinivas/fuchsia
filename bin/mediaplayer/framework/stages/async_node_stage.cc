// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/stages/async_node_stage.h"

#include "garnet/bin/mediaplayer/framework/formatting.h"

namespace media_player {

AsyncNodeStageImpl::AsyncNodeStageImpl(std::shared_ptr<AsyncNode> node)
    : node_(node) {
  FXL_DCHECK(node_);

  size_t input_count;
  size_t output_count;
  node_->GetConfiguration(&input_count, &output_count);

  inputs_.reserve(input_count);
  for (size_t input_index = 0; input_index < input_count; ++input_index) {
    inputs_.emplace_back(this, input_index);
  }

  outputs_.reserve(output_count);
  for (size_t output_index = 0; output_index < output_count; ++output_index) {
    outputs_.emplace_back(this, output_index);
  }

  std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
  packets_per_output_.resize(output_count);
}

AsyncNodeStageImpl::~AsyncNodeStageImpl() {}

void AsyncNodeStageImpl::Dump(std::ostream& os) const {
  if (inputs_.size() == 1) {
    os << fostr::NewLine << "input:";
    DumpInputDetail(os, inputs_[0]);
  } else if (inputs_.size() > 1) {
    os << fostr::NewLine << "inputs:";
    int index = 0;
    for (auto& input : inputs_) {
      os << fostr::NewLine << "[" << index++ << "] ";
      DumpInputDetail(os, input);
    }
  }

  if (outputs_.size() == 1) {
    os << fostr::NewLine << "output:";
    DumpOutputDetail(os, outputs_[0]);
  } else if (outputs_.size() > 1) {
    os << fostr::NewLine << "outputs:";
    int index = 0;
    for (auto& output : outputs_) {
      os << fostr::NewLine << "[" << index++ << "] ";
      DumpOutputDetail(os, output);
    }
  }
}

void AsyncNodeStageImpl::DumpInputDetail(std::ostream& os,
                                         const Input& input) const {
  os << fostr::Indent;
  if (input.connected()) {
    os << fostr::NewLine << "connected to:  " << *input.mate();
  } else {
    os << fostr::NewLine << "connected to:  <nothing>";
  }

  os << fostr::NewLine << "prepared:      " << input.prepared();
  os << fostr::NewLine << "needs packet:  " << input.needs_packet();
  os << fostr::NewLine << "packet:        " << input.packet();
  os << fostr::Outdent;
}

void AsyncNodeStageImpl::DumpOutputDetail(std::ostream& os,
                                          const Output& output) const {
  os << fostr::Indent;
  os << fostr::NewLine << "needs packet:  " << output.needs_packet();

  std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
  auto& packets = packets_per_output_[output.index()];
  if (!packets.empty()) {
    os << fostr::NewLine << "queued packets:" << fostr::Indent;

    for (auto& packet : packets) {
      os << fostr::NewLine << packet;
    }

    os << fostr::Outdent;
  }

  if (output.connected()) {
    os << fostr::NewLine << "connected to:  " << *output.mate();
  } else {
    os << fostr::NewLine << "connected to:  <nothing>";
  }

  os << fostr::Outdent;
}

void AsyncNodeStageImpl::OnShutDown() {}

size_t AsyncNodeStageImpl::input_count() const { return inputs_.size(); };

Input& AsyncNodeStageImpl::input(size_t input_index) {
  FXL_DCHECK(input_index < inputs_.size());
  return inputs_[input_index];
}

size_t AsyncNodeStageImpl::output_count() const { return outputs_.size(); }

Output& AsyncNodeStageImpl::output(size_t output_index) {
  FXL_DCHECK(output_index < outputs_.size());
  return outputs_[output_index];
}

std::shared_ptr<PayloadAllocator> AsyncNodeStageImpl::PrepareInput(
    size_t input_index) {
  FXL_DCHECK(input_index < inputs_.size());
  return node_->allocator_for_input(input_index);
}

void AsyncNodeStageImpl::PrepareOutput(
    size_t output_index, std::shared_ptr<PayloadAllocator> allocator) {
  FXL_DCHECK(output_index < outputs_.size());

  if (node_->can_accept_allocator_for_output(output_index)) {
    // Give the node the provided allocator or a default allocator if none was
    // provided.
    node_->SetAllocatorForOutput(
        allocator == nullptr ? PayloadAllocator::CreateDefault() : allocator,
        output_index);
  } else if (allocator) {
    // The node can't use the provided allocator, so the output must copy
    // packets.
    outputs_[output_index].SetCopyAllocator(allocator);
  }
}

void AsyncNodeStageImpl::UnprepareOutput(size_t output_index) {
  FXL_DCHECK(output_index < outputs_.size());

  if (node_->can_accept_allocator_for_output(output_index)) {
    // Outputs for which |can_accept_allocator_for_output| returns false will
    // typically DCHECK if asked to |SetAllocatorForOutput|, hence the check
    // above.
    node_->SetAllocatorForOutput(nullptr, output_index);
  }

  outputs_[output_index].SetCopyAllocator(nullptr);
}

GenericNode* AsyncNodeStageImpl::GetGenericNode() const { return node_.get(); }

void AsyncNodeStageImpl::Update() {
  FXL_DCHECK(node_);

  for (auto& input : inputs_) {
    if (input.packet()) {
      node_->PutInputPacket(input.TakePacket(false), input.index());
    }
  }

  bool request_packet = false;

  for (auto& output : outputs_) {
    if (!output.connected()) {
      continue;
    }

    PacketPtr packet_to_supply;
    if (MaybeTakePacketForOutput(output, &packet_to_supply)) {
      request_packet = true;
    }

    if (packet_to_supply) {
      output.SupplyPacket(std::move(packet_to_supply));
    }
  }

  if (request_packet) {
    node_->RequestOutputPacket();
  }
}

bool AsyncNodeStageImpl::MaybeTakePacketForOutput(const Output& output,
                                                  PacketPtr* packet_out) {
  FXL_DCHECK(packet_out);

  if (!output.needs_packet()) {
    return false;
  }

  bool request_packet = false;

  std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
  std::deque<PacketPtr>& packets = packets_per_output_[output.index()];

  if (packets.empty()) {
    // The output needs a packet and has no packets queued. Request another
    // packet so we can meet the demand.
    request_packet = true;
  } else {
    // The output has demand and packets queued.
    *packet_out = std::move(packets.front());
    packets.pop_front();
  }

  return request_packet;
}

void AsyncNodeStageImpl::FlushInput(size_t input_index, bool hold_frame,
                                    fit::closure callback) {
  FXL_DCHECK(input_index < inputs_.size());

  inputs_[input_index].Flush();

  node_->FlushInput(hold_frame, input_index,
                    [this, callback = std::move(callback)]() mutable {
                      PostTask(std::move(callback));
                    });
}

void AsyncNodeStageImpl::FlushOutput(size_t output_index,
                                     fit::closure callback) {
  FXL_DCHECK(output_index < outputs_.size());

  node_->FlushOutput(output_index, [this, output_index,
                                    callback = std::move(callback)]() mutable {
    {
      std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
      auto& packets = packets_per_output_[output_index];
      while (!packets.empty()) {
        packets.pop_front();
      }
    }

    PostTask(std::move(callback));
  });
}

void AsyncNodeStageImpl::PostTask(fit::closure task) {
  // This method runs on an arbitrary thread.
  StageImpl::PostTask(std::move(task));
}

void AsyncNodeStageImpl::RequestInputPacket(size_t input_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(input_index < inputs_.size());

  inputs_[input_index].RequestPacket();
}

void AsyncNodeStageImpl::PutOutputPacket(PacketPtr packet,
                                         size_t output_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(packet);
  FXL_DCHECK(output_index < outputs_.size());

  // Queue the packet if the output is connected, otherwise discard the
  // packet.
  if (outputs_[output_index].connected()) {
    std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
    packets_per_output_[output_index].push_back(std::move(packet));
  }

  NeedsUpdate();
}

}  // namespace media_player
