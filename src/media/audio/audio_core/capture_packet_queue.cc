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
// we can allocate ~1000 packets. At 10ms per packet, that is ~10s of audio.
static const size_t kMaxSlabs = static_cast<size_t>(
    ceil(1000.0 / (static_cast<double>(fbl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE) / sizeof(Packet))));
static const size_t kMaxPackets =
    (kMaxSlabs * fbl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE) / sizeof(Packet);

namespace media::audio {

CapturePacketQueue::CapturePacketQueue(Mode mode, const fzl::VmoMapper& payload_buffer,
                                       const Format& format)
    : mode_(mode),
      payload_buffer_(payload_buffer),
      payload_buffer_frames_(payload_buffer.size() / format.bytes_per_frame()),
      format_(format),
      allocator_(kMaxSlabs, true) {}

fit::result<std::shared_ptr<CapturePacketQueue>, std::string>
CapturePacketQueue::CreatePreallocated(const fzl::VmoMapper& payload_buffer, const Format& format,
                                       size_t frames_per_packet) {
  auto out = std::make_shared<CapturePacketQueue>(Mode::Preallocated, payload_buffer, format);

  // Locking is not strictly necessary here, but it makes the lock analysis simpler.
  std::lock_guard<std::mutex> lock(out->mutex_);

  // Sanity check the number of frames per packet the user is asking for.
  //
  // Currently our minimum frames-per-packet is 1, which is absurdly low.
  // TODO(fxbug.dev/13344): Decide on a proper minimum packet size, document it, and enforce the
  // limit here.
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
  for (size_t frame = 0; frame + frames_per_packet <= out->payload_buffer_frames_;
       frame += frames_per_packet) {
    auto p = out->Alloc(frame, frames_per_packet, nullptr);
    if (!p) {
      return fit::error(fxl::StringPrintf(
          "packet queue is too large; exceeded limit after %lu packets", kMaxPackets));
    }
    out->pending_.push_back(p);
  }

  return fit::ok(std::move(out));
}

std::shared_ptr<CapturePacketQueue> CapturePacketQueue::CreateDynamicallyAllocated(
    const fzl::VmoMapper& payload_buffer, const Format& format) {
  return std::make_shared<CapturePacketQueue>(Mode::DynamicallyAllocated, payload_buffer, format);
}

fbl::RefPtr<Packet> CapturePacketQueue::Alloc(size_t offset_frames, size_t num_frames,
                                              CaptureAtCallback callback) {
  size_t payload_offset = offset_frames * format_.bytes_per_frame();
  char* payload_start = static_cast<char*>(payload_buffer_.start()) + payload_offset;
  return allocator_.New(std::move(callback), num_frames, payload_offset, payload_start);
}

bool CapturePacketQueue::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.empty() && ready_.empty();
}

size_t CapturePacketQueue::PendingSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.size();
}

size_t CapturePacketQueue::ReadySize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ready_.size();
}

std::optional<CapturePacketQueue::PacketMixState> CapturePacketQueue::NextMixerJob() {
  TRACE_INSTANT("audio", "CapturePacketQueue::NextMixerJob", TRACE_SCOPE_THREAD);
  std::lock_guard<std::mutex> lock(mutex_);
  if (shutdown_ || pending_.empty()) {
    return std::nullopt;
  }
  auto p = pending_.front();
  return PacketMixState{
      .packet = p,
      .capture_timestamp = p->state_.capture_timestamp,
      .flags = p->state_.flags,
      .target = p->payload_buffer_start_ + p->state_.filled_frames * format_.bytes_per_frame(),
      .frames = p->num_frames_ - p->state_.filled_frames,
  };
}

CapturePacketQueue::PacketMixStatus CapturePacketQueue::FinishMixerJob(
    const PacketMixState& state) {
  TRACE_INSTANT("audio", "CapturePacketQueue::FinishMixerJob", TRACE_SCOPE_THREAD);
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_.empty() || pending_.front() != state.packet) {
    return PacketMixStatus::Discarded;
  }
  auto& p = state.packet;
  p->state_.capture_timestamp = state.capture_timestamp;
  p->state_.flags = state.flags;
  p->state_.filled_frames += state.frames;
  if (p->state_.filled_frames < p->num_frames_) {
    return PacketMixStatus::Partial;
  }
  PopPendingLocked();
  return PacketMixStatus::Done;
}

void CapturePacketQueue::DiscardPendingPackets() {
  TRACE_INSTANT("audio", "CapturePacketQueue::DiscardPendingPackets", TRACE_SCOPE_THREAD);
  std::lock_guard<std::mutex> lock(mutex_);
  while (!pending_.empty()) {
    PopPendingLocked();
  }
}

void CapturePacketQueue::PopPendingLocked() {
  // Caller must acquire the lock.
  auto p = pending_.front();

  // Now that this packet is ready, create the final StreamPacket.
  auto& pkt = p->stream_packet_;
  pkt.pts = p->state_.capture_timestamp;
  pkt.flags = p->state_.flags;
  pkt.payload_buffer_id = 0u;
  pkt.payload_offset = p->payload_buffer_offset_;
  pkt.payload_size = p->state_.filled_frames * format_.bytes_per_frame();

  // Move to the ready queue.
  pending_.pop_front();
  ready_.push_back(p);
  p->ready_time_ = zx::clock::get_monotonic();
  p->ready_.store(true);
}

fbl::RefPtr<CapturePacketQueue::Packet> CapturePacketQueue::PopReady() {
  TRACE_INSTANT("audio", "CapturePacketQueue::PopReady", TRACE_SCOPE_THREAD);
  std::lock_guard<std::mutex> lock(mutex_);
  if (ready_.empty()) {
    return nullptr;
  }
  auto p = ready_.front();
  ready_.pop_front();
  if (mode_ == Mode::Preallocated) {
    // In preallocated mode, we retain a reference so the packet can be recycled.
    inflight_[p->stream_packet_.payload_offset] = p;
  }
  return p;
}

fit::result<void, std::string> CapturePacketQueue::PushPending(size_t offset_frames,
                                                               size_t num_frames,
                                                               CaptureAtCallback callback) {
  TRACE_INSTANT("audio", "CapturePacketQueue::PushPending", TRACE_SCOPE_THREAD);
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

  auto p = Alloc(offset_frames, num_frames, std::move(callback));
  if (!p) {
    return fit::error(fxl::StringPrintf(
        "packet queue is too large; exceeded limit after %lu packets", kMaxPackets));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!shutdown_) {
    pending_.push_back(p);
    pending_signal_.notify_all();
  }
  return fit::ok();
}

fit::result<void, std::string> CapturePacketQueue::Recycle(const StreamPacket& stream_packet) {
  TRACE_INSTANT("audio", "CapturePacketQueue::Recycle", TRACE_SCOPE_THREAD);
  FX_CHECK(mode_ == Mode::Preallocated);

  std::lock_guard<std::mutex> lock(mutex_);
  if (shutdown_) {
    return fit::ok();
  }
  auto it = inflight_.find(stream_packet.payload_offset);
  if (it == inflight_.end()) {
    return fit::error(
        fxl::StringPrintf("could not release unknown packet with payload_offset = %lu",
                          stream_packet.payload_offset));
  }
  auto p = it->second;
  if (p->stream_packet_.payload_buffer_id != stream_packet.payload_buffer_id ||
      p->stream_packet_.payload_offset != stream_packet.payload_offset ||
      p->stream_packet_.payload_size != stream_packet.payload_size) {
    return fit::error(fxl::StringPrintf(
        "could not release packet with payload { buffer_id = %u, offset = %lu, size = %lu }, "
        "expected packet with payload { buffer_id = %u, offset = %lu, size = %lu }",
        stream_packet.payload_buffer_id, stream_packet.payload_offset, stream_packet.payload_size,
        p->stream_packet_.payload_buffer_id, p->stream_packet_.payload_offset,
        p->stream_packet_.payload_size));
  }

  // Move from inflight to pending.
  p->Reset();
  pending_.push_back(p);
  pending_signal_.notify_all();
  inflight_.erase(it);
  return fit::ok();
}

void CapturePacketQueue::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  shutdown_ = true;
  pending_signal_.notify_all();
}

namespace {
// WaitForPendingPacket uses std::unique_lock as required by std::condition_variable::wait.
// Unfortunately, std::unique_lock supports optional locking, which is not supported by clang's
// thread annotations. We use std::unique_lock in a purely scoped way, which is supported,
// so we wrap it with a simple class annotated as a scoped lock.
class FXL_SCOPED_LOCKABLE scoped_unique_lock : public std::unique_lock<std::mutex> {
 public:
  explicit scoped_unique_lock(std::mutex& m) FXL_ACQUIRE(m) : std::unique_lock<std::mutex>(m) {}
  ~scoped_unique_lock() FXL_RELEASE() { std::unique_lock<std::mutex>::~unique_lock(); }
};
}  // namespace

void CapturePacketQueue::WaitForPendingPacket() {
  scoped_unique_lock lock(mutex_);
  while (!shutdown_ && pending_.empty()) {
    pending_signal_.wait(lock);
  }
}

}  // namespace media::audio
