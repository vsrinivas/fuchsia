// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/capture_packet_queue.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/format/format.h"

using Packet = media::audio::CapturePacketQueue::Packet;

// The maximum number of slabs per each capture queue. Allow enough slabs so
// we can allocate ~500 packets. At 10ms per packet, that is ~5s of audio.
static const size_t kMaxSlabs = static_cast<size_t>(
    ceil(500.0 / (static_cast<double>(fbl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE) / sizeof(Packet))));
static const size_t kMaxPackets =
    (kMaxSlabs * fbl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE) / sizeof(Packet);

namespace media::audio {

bool CapturePacketQueue::must_release_packets_ = false;

CapturePacketQueue::CapturePacketQueue(Mode mode, const fzl::VmoMapper& payload_buffer,
                                       const Format& format)
    : mode_(mode),
      payload_buffer_(payload_buffer),
      payload_buffer_frames_(payload_buffer.size() / format.bytes_per_frame()),
      format_(format),
      allocator_(kMaxSlabs, true) {}

fit::result<std::unique_ptr<CapturePacketQueue>, std::string>
CapturePacketQueue::CreatePreallocated(const fzl::VmoMapper& payload_buffer, const Format& format,
                                       size_t frames_per_packet) {
  auto out = std::make_unique<CapturePacketQueue>(Mode::Preallocated, payload_buffer, format);

  // Locking is not strictly necessary here, but it makes the lock analysis simpler.
  std::lock_guard<std::mutex> lock(out->mutex_);

  // Sanity check the number of frames per packet the user is asking for.
  //
  // Currently our minimum frames-per-packet is 1, which is absurdly low.
  // TODO(13344): Decide on a proper minimum packet size, document it, and enforce the limit here.
  if (frames_per_packet == 0) {
    return fit::error("frames per packet may not be zero");
  }

  if (frames_per_packet > (out->payload_buffer_frames_ / 2)) {
    return fit::error(fxl::StringPrintf(
        "there must be enough room in the shared payload buffer (%lu frames) to fit at least two "
        "packets of the requested number of frames per packet (%lu frames).",
        out->payload_buffer_frames_, frames_per_packet));
  }

  // Pre-allocate every packet.
  for (size_t frame = 0; frame < out->payload_buffer_frames_; frame += frames_per_packet) {
    auto p = out->Alloc(frame, frames_per_packet);
    if (!p) {
      return fit::error(fxl::StringPrintf(
          "packet queue is too large; exceeded limit after %lu packets", kMaxPackets));
    }
    out->pending_.push_back(p);
  }

  return fit::ok(std::move(out));
}

fit::result<std::unique_ptr<CapturePacketQueue>, std::string>
CapturePacketQueue::CreateDynamicallyAllocated(const fzl::VmoMapper& payload_buffer,
                                               const Format& format) {
  return fit::ok(
      std::make_unique<CapturePacketQueue>(Mode::DynamicallyAllocated, payload_buffer, format));
}

fbl::RefPtr<Packet> CapturePacketQueue::Alloc(size_t offset_frames, size_t num_frames) {
  auto p = allocator_.New();
  p->stream_packet = {
      .payload_buffer_id = 0,
      .payload_offset = offset_frames * format_.bytes_per_frame(),
      .payload_size = num_frames * format_.bytes_per_frame(),
  };
  auto start = static_cast<char*>(payload_buffer_.start());
  p->buffer_start = start + p->stream_packet.payload_offset;
  p->buffer_end = start + p->stream_packet.payload_offset + p->stream_packet.payload_size;
  return p;
}

size_t CapturePacketQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.size();
}

fbl::RefPtr<CapturePacketQueue::Packet> CapturePacketQueue::Front() const {
  std::lock_guard<std::mutex> lock(mutex_);
  FX_CHECK(!pending_.empty());
  return pending_.front();
}

fbl::RefPtr<CapturePacketQueue::Packet> CapturePacketQueue::Pop() {
  TRACE_INSTANT("audio", "CapturePacketQueue::Pop", TRACE_SCOPE_PROCESS);
  std::lock_guard<std::mutex> lock(mutex_);
  FX_CHECK(!pending_.empty());
  return PopLocked();
}

std::vector<fbl::RefPtr<CapturePacketQueue::Packet>> CapturePacketQueue::PopAll() {
  TRACE_INSTANT("audio", "CapturePacketQueue::PopAll", TRACE_SCOPE_PROCESS);
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<fbl::RefPtr<Packet>> out;
  while (!pending_.empty()) {
    out.push_back(PopLocked());
  }
  return out;
}

fbl::RefPtr<CapturePacketQueue::Packet> CapturePacketQueue::PopLocked() {
  // Caller must acquire the lock.
  auto p = pending_.front();
  pending_.pop_front();
  if (mode_ == Mode::Preallocated) {
    // In preallocated mode, we always retain at least one reference to every packet.
    if (must_release_packets_) {
      inflight_[p->stream_packet.payload_offset] = p;
    } else {
      pending_.push_back(p);
    }
  }
  return p;
}

fit::result<void, std::string> CapturePacketQueue::Push(size_t offset_frames, size_t num_frames,
                                                        CaptureAtCallback callback) {
  TRACE_INSTANT("audio", "CapturePacketQueue::Push", TRACE_SCOPE_PROCESS);
  FX_CHECK(mode_ == Mode::DynamicallyAllocated);

  // Buffers submitted by clients must exist entirely within the shared payload buffer, and must
  // have at least some payloads in them.
  uint64_t offset_frames_end = static_cast<uint64_t>(offset_frames) + num_frames;
  if (!num_frames || (offset_frames_end > payload_buffer_frames_)) {
    return fit::error(
        fxl::StringPrintf("cannot push buffer range { offset = %lu, num_frames = %lu } into shared "
                          "buffer with %lu frames",
                          offset_frames, num_frames, payload_buffer_frames_));
  }

  auto p = Alloc(offset_frames, num_frames);
  if (!p) {
    return fit::error(fxl::StringPrintf(
        "packet queue is too large; exceeded limit after %lu packets", kMaxPackets));
  }
  p->callback = std::move(callback);

  std::lock_guard<std::mutex> lock(mutex_);
  pending_.push_back(p);
  return fit::ok();
}

fit::result<void, std::string> CapturePacketQueue::Release(const StreamPacket& stream_packet) {
  TRACE_INSTANT("audio", "CapturePacketQueue::Release", TRACE_SCOPE_PROCESS);
  FX_CHECK(mode_ == Mode::Preallocated);
  if (!must_release_packets_) {
    return fit::ok();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = inflight_.find(stream_packet.payload_offset);
  if (it == inflight_.end()) {
    return fit::error(
        fxl::StringPrintf("could not release unknown packet with payload_offset = %lu",
                          stream_packet.payload_offset));
  }
  if (it->second->stream_packet.payload_buffer_id != stream_packet.payload_buffer_id ||
      it->second->stream_packet.payload_offset != stream_packet.payload_offset ||
      it->second->stream_packet.payload_size != stream_packet.payload_size) {
    return fit::error(fxl::StringPrintf(
        "could not release packet with payload { buffer_id = %u, offset = %lu, size = %lu }, "
        "expected packet with payload { buffer_id = %u, offset = %lu, size = %lu }",
        stream_packet.payload_buffer_id, stream_packet.payload_offset, stream_packet.payload_size,
        it->second->stream_packet.payload_buffer_id, it->second->stream_packet.payload_offset,
        it->second->stream_packet.payload_size));
  }

  // Move from inflight to pending.
  pending_.push_back(it->second);
  inflight_.erase(it);
  return fit::ok();
}

}  // namespace media::audio
