// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/stream_sink_client.h"

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

StreamSinkClient::StreamSinkClient(Args args)
    : payload_buffers_(std::move(args.payload_buffers)),
      recycled_packet_queue_(std::move(args.recycled_packet_queue)),
      thread_(std::move(args.thread)),
      client_(fidl::WireSharedClient(std::move(args.client_end), thread_->dispatcher())) {
  FX_CHECK(args.frames_per_packet > 0);

  // Subdivide each payload buffer into an integer number of packets.
  const size_t bytes_per_packet =
      static_cast<size_t>(args.format.bytes_per_frame() * args.frames_per_packet);

  // `payload_buffers_` is a map instead of an unordered_map so that this loop is deterministic.
  // Since we never lookup a buffer by id, there's no benefit to using an unordered_map.
  for (auto& [id, buffer] : payload_buffers_) {
    const size_t packet_count = buffer->content_size() / bytes_per_packet;
    FX_CHECK(packet_count > 0);

    for (size_t k = 0; k < packet_count; k++) {
      const size_t offset = k * bytes_per_packet;
      const fuchsia_media2::wire::PayloadRange payload_range{
          .buffer_id = id,
          .offset = offset,
          .size = bytes_per_packet,
      };
      recycled_packet_queue_->push(
          std::make_unique<Packet>(buffer, payload_range, buffer->offset(offset)));
    }
  }
}

void StreamSinkClient::PutPacket(std::unique_ptr<Packet> packet) {
  thread_->PostTask([this, self = shared_from_this(), packet = std::move(packet)]() mutable {
    ScopedThreadChecker checker(thread().checker());

    // Ignore if shutting down.
    if (!client_) {
      return;
    }

    // This should not fail.
    zx::eventpair local_fence;
    zx::eventpair remote_fence;
    auto status = zx::eventpair::create(0, &local_fence, &remote_fence);
    FX_CHECK(status == ZX_OK) << "zx::eventpair::create failed with status " << status;

    // Make the FIDL call.
    fidl::Arena<> arena;
    auto request = fuchsia_audio::wire::StreamSinkPutPacketRequest::Builder(arena)
                       .packet(packet->ToFidl(arena))
                       .release_fence(std::move(remote_fence))
                       .Build();
    if (auto result = (*client_)->PutPacket(request); !result.ok()) {
      FX_LOGS(INFO) << "StreamSinkClient connection closed: " << result;
      client_ = std::nullopt;
      return;
    }

    // Wait for the fence to be released.
    auto inflight = std::make_unique<InflightPacket>(shared_from_this(), std::move(packet),
                                                     std::move(local_fence));
    auto inflight_ptr = inflight.get();
    auto [it, inserted] = inflight_packets_.emplace(inflight_ptr, std::move(inflight));
    FX_CHECK(inserted);
    status = it->second->wait.Begin(
        thread().dispatcher(), [inflight_ptr](auto unused_dispatcher, auto unused_wait,
                                              zx_status_t status, auto unused_signal) mutable {
          // If the dispatcher is shutting down, `inflight_packets_` has been cleared.
          if (status != ZX_OK) {
            return;
          }

          auto& client = inflight_ptr->client;
          ScopedThreadChecker checker(client->thread().checker());

          auto it = client->inflight_packets_.find(inflight_ptr);
          FX_CHECK(it != client->inflight_packets_.end());

          client->recycled_packet_queue_->push(std::move(it->second->packet));
          client->inflight_packets_.erase(it);
        });
    if (status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "WaitOnce.Begin failed";
    }
  });
}

void StreamSinkClient::End() {
  thread_->PostTask([this, self = shared_from_this()]() {
    ScopedThreadChecker checker(thread().checker());

    // Ignore if shutting down.
    if (!client_) {
      return;
    }

    // `End` is an endpoint of the current audio stream, but not necessarily of the channel itself.
    // Another audio stream may start on the same channel. For example, if the client starts, stops,
    // then starts a Consumer, we'll send `End` when stopping, then when restarting, we'll send
    // `PutPacket` messages on the same channel. Hence we don't discard `client_` after `End` unless
    // our peer has closed the connection.
    if (auto result = (*client_)->End(); !result.ok()) {
      FX_LOGS(INFO) << "StreamSinkClient connection closed: " << result;
      client_ = std::nullopt;
    }
  });
}

void StreamSinkClient::Shutdown() {
  client_ = std::nullopt;
  // After shutting down, packets no longer need to be recycled. Also, each `InflightPacket.wait`
  // contains a closure which holds onto `shared_from_this()`, creating a circular reference.
  // Discarding those closures is necessary to break that shared_ptr cycle.
  inflight_packets_.clear();
}

}  // namespace media_audio
