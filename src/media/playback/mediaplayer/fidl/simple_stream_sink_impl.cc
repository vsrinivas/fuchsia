// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/simple_stream_sink_impl.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_buffer.h"

namespace media_player {

// static
std::shared_ptr<SimpleStreamSinkImpl> SimpleStreamSinkImpl::Create(
    const StreamType& output_stream_type, media::TimelineRate pts_rate,
    fidl::InterfaceRequest<fuchsia::media::SimpleStreamSink> request,
    fit::closure connection_failure_callback) {
  FX_DCHECK(request);
  return std::make_shared<SimpleStreamSinkImpl>(output_stream_type, pts_rate, std::move(request),
                                                std::move(connection_failure_callback));
}

// static
std::shared_ptr<SimpleStreamSinkImpl> SimpleStreamSinkImpl::Create(
    const StreamType& output_stream_type, media::TimelineRate pts_rate,
    fidl::InterfaceRequest<fuchsia::media::StreamSink> request,
    fit::closure connection_failure_callback) {
  FX_DCHECK(request);

  fidl::InterfaceRequest<fuchsia::media::SimpleStreamSink> simple_stream_sink_request(
      request.TakeChannel());
  return SimpleStreamSinkImpl::Create(output_stream_type, pts_rate,
                                      std::move(simple_stream_sink_request),
                                      std::move(connection_failure_callback));
}

SimpleStreamSinkImpl::SimpleStreamSinkImpl(
    const StreamType& output_stream_type, media::TimelineRate pts_rate,
    fidl::InterfaceRequest<fuchsia::media::SimpleStreamSink> request,
    fit::closure connection_failure_callback)
    : output_stream_type_(output_stream_type.Clone()),
      pts_rate_(pts_rate),
      binding_(this, std::move(request)),
      connection_failure_callback_(std::move(connection_failure_callback)) {
  FX_DCHECK(output_stream_type_);
  FX_DCHECK(binding_.is_bound());

  binding_.set_error_handler([&](zx_status_t status) {
    if (connection_failure_callback_) {
      connection_failure_callback_();
    }
  });
}

SimpleStreamSinkImpl::~SimpleStreamSinkImpl() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
}

void SimpleStreamSinkImpl::Dump(std::ostream& os) const {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  os << label() << fostr::Indent;
  Node::Dump(os);
  os << fostr::NewLine << "flushing: " << flushing_;
  os << fostr::NewLine << "last pts: " << AsNs(pts_);
  os << fostr::NewLine << "payload vmos:";
  for (auto& [id, info] : payload_vmo_infos_by_id_) {
    os << fostr::NewLine << "[" << id << "] " << info.vmo_;
    if (info.packet_count_ != 0) {
      os << "(" << info.packet_count_ << " packets outstanding)";
    }
  }

  os << fostr::Outdent;
}

void SimpleStreamSinkImpl::ConfigureConnectors() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  ConfigureOutputToProvideVmos(VmoAllocation::kUnrestricted);
}

void SimpleStreamSinkImpl::FlushOutput(size_t output_index, fit::closure callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FX_DCHECK(output_index == 0);
  FX_DCHECK(callback);

  // TODO(dalesat): The client will need to know about this.
  flushing_ = true;
  callback();
}

void SimpleStreamSinkImpl::RequestOutputPacket() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  if (flushing_) {
    // TODO(dalesat): The client will need to know about this.
    flushing_ = false;
  }

  // There's nothing else we can do about this. The client provides packets at
  // will.
}

void SimpleStreamSinkImpl::AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (payload_vmo_infos_by_id_.find(id) != payload_vmo_infos_by_id_.end()) {
    FX_LOGS(ERROR) << "AddPayloadBuffer: payload buffer with id " << id
                   << " already exists. Closing connection.";
    binding_.Unbind();
    return;
  }

  auto payload_vmo = PayloadVmo::Create(std::move(payload_buffer), ZX_VM_PERM_READ);
  if (!payload_vmo) {
    FX_LOGS(ERROR) << "AddPayloadBuffer: cannot map VMO for reading.";
    binding_.Unbind();
    return;
  }

  payload_vmo_infos_by_id_.emplace(id, PayloadVmoInfo{.vmo_ = payload_vmo});

  ProvideOutputVmos().AddVmo(payload_vmo);
}

void SimpleStreamSinkImpl::RemovePayloadBuffer(uint32_t id) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  auto iter = payload_vmo_infos_by_id_.find(id);
  if (iter == payload_vmo_infos_by_id_.end()) {
    FX_LOGS(ERROR) << "RemovePayloadBuffer: no payload buffer with id " << id
                   << " exists. Closing connection.";
    binding_.Unbind();
    return;
  }

  auto& payload_vmo_info = iter->second;

  if (payload_vmo_info.packet_count_ != 0) {
    FX_LOGS(ERROR) << "RemovePayloadBuffer: payload buffer " << id
                   << " has pending StreamPackets. Closing connection.";
    binding_.Unbind();
    return;
  }

  ProvideOutputVmos().RemoveVmo(payload_vmo_info.vmo_);
  payload_vmo_infos_by_id_.erase(iter);
}

void SimpleStreamSinkImpl::SendPacket(fuchsia::media::StreamPacket packet,
                                      SendPacketCallback callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  // |callback| is nullptr when |SendPacketNoReply| calls this method.

  if (flushing_) {
    // We're flushing at the moment, so release the packet immediately.
    if (callback) {
      callback();
    }

    return;
  }

  uint32_t vmo_id = packet.payload_buffer_id;
  int64_t payload_offset = packet.payload_offset;

  auto iter = payload_vmo_infos_by_id_.find(vmo_id);
  if (iter == payload_vmo_infos_by_id_.end()) {
    FX_LOGS(ERROR) << "SendPacket: no payload buffer with id " << vmo_id
                   << " exists. Closing connection.";
    binding_.Unbind();
    return;
  }

  auto& payload_vmo_info = iter->second;

  if (payload_offset + packet.payload_size > payload_vmo_info.vmo_->size()) {
    FX_LOGS(ERROR) << "SendPacket: packet offset/size out of range.";
    binding_.Unbind();
    return;
  }

  ++payload_vmo_info.packet_count_;

  auto payload_buffer = PayloadBuffer::Create(
      packet.payload_size, payload_vmo_info.vmo_->at_offset(payload_offset), payload_vmo_info.vmo_,
      payload_offset,
      [this, shared_this = shared_from_this(), vmo_id,
       callback = std::move(callback)](PayloadBuffer* payload_buffer) mutable {
        PostTask([this, shared_this, vmo_id, callback = std::move(callback)]() {
          auto iter = payload_vmo_infos_by_id_.find(vmo_id);
          FX_DCHECK(iter != payload_vmo_infos_by_id_.end());
          auto& payload_vmo_info = iter->second;
          FX_DCHECK(payload_vmo_info.vmo_);
          FX_DCHECK(payload_vmo_info.packet_count_ != 0);

          --payload_vmo_info.packet_count_;

          if (callback) {
            callback();
          }
        });
      });

  PutOutputPacket(Packet::Create(
      packet.pts, pts_rate_, (packet.flags & fuchsia::media::STREAM_PACKET_FLAG_KEY_FRAME) != 0,
      (packet.flags & fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY) != 0,
      false,  // end_of_stream
      packet.payload_size, payload_buffer));

  pts_ = packet.pts;
}

void SimpleStreamSinkImpl::SendPacketNoReply(fuchsia::media::StreamPacket packet) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  SendPacket(std::move(packet), nullptr);
}

void SimpleStreamSinkImpl::EndOfStream() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  PutOutputPacket(Packet::CreateEndOfStream(pts_, pts_rate_));
}

void SimpleStreamSinkImpl::DiscardAllPackets(DiscardAllPacketsCallback callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  // |callback| is nullptr when |DiscardAllPacketsNoReply| calls this method.
  // TODO(dalesat): Implement.
}

void SimpleStreamSinkImpl::DiscardAllPacketsNoReply() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  DiscardAllPackets(nullptr);
}

}  // namespace media_player
