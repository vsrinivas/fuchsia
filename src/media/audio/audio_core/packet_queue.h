// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PACKET_QUEUE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PACKET_QUEUE_H_

#include <deque>
#include <memory>
#include <mutex>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/packet.h"
#include "src/media/audio/audio_core/pending_flush_token.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"

namespace media::audio {

class PacketQueue : public Stream {
 public:
  explicit PacketQueue(Format format);
  PacketQueue(Format format,
              fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames);
  ~PacketQueue();

  bool empty() const {
    std::lock_guard<std::mutex> locker(pending_mutex_);
    return pending_packet_queue_.empty();
  }

  void PushPacket(const fbl::RefPtr<Packet>& packet);
  void Flush(const fbl::RefPtr<PendingFlushToken>& flush_token = nullptr);

  // |media::audio::Stream|
  std::optional<Stream::Buffer> LockBuffer() override;
  void UnlockBuffer(bool release_buffer) override;
  std::pair<TimelineFunction, uint32_t> ReferenceClockToFractionalFrames() const override;
  void ReportUnderflow(FractionalFrames<int64_t> frac_source_start,
                       FractionalFrames<int64_t> frac_source_mix_point,
                       zx::duration underflow_duration) override;
  void ReportPartialUnderflow(FractionalFrames<int64_t> frac_source_offset,
                              int64_t dest_mix_offset) override;

 private:
  std::mutex flush_mutex_;
  mutable std::mutex pending_mutex_;

  std::deque<fbl::RefPtr<Packet>> pending_packet_queue_ FXL_GUARDED_BY(pending_mutex_);
  std::deque<fbl::RefPtr<Packet>> pending_flush_packet_queue_ FXL_GUARDED_BY(pending_mutex_);
  std::deque<fbl::RefPtr<PendingFlushToken>> pending_flush_token_queue_
      FXL_GUARDED_BY(pending_mutex_);
  bool flushed_ FXL_GUARDED_BY(pending_mutex_) = true;
  bool processing_in_progress_ FXL_GUARDED_BY(pending_mutex_) = false;
  fbl::RefPtr<VersionedTimelineFunction> timeline_function_;
  std::atomic<uint16_t> underflow_count_ = {0};
  std::atomic<uint16_t> partial_underflow_count_ = {0};
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PACKET_QUEUE_H_
