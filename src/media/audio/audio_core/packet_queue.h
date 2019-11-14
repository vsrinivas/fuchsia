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

namespace media::audio {

class PacketQueue : public Stream {
 public:
  PacketQueue(Format format);
  ~PacketQueue();

  bool empty() const {
    std::lock_guard<std::mutex> locker(pending_mutex_);
    return pending_packet_queue_.empty();
  }

  void PushPacket(const fbl::RefPtr<Packet>& packet);
  void Flush(const fbl::RefPtr<PendingFlushToken>& flush_token = nullptr);

  // |media::audio::Stream|
  fbl::RefPtr<Packet> LockPacket(bool* was_flushed) override;
  void UnlockPacket(bool release_packet) override;

 private:
  std::mutex flush_mutex_;
  mutable std::mutex pending_mutex_;

  std::deque<fbl::RefPtr<Packet>> pending_packet_queue_ FXL_GUARDED_BY(pending_mutex_);
  std::deque<fbl::RefPtr<Packet>> pending_flush_packet_queue_ FXL_GUARDED_BY(pending_mutex_);
  std::deque<fbl::RefPtr<PendingFlushToken>> pending_flush_token_queue_
      FXL_GUARDED_BY(pending_mutex_);
  bool flushed_ FXL_GUARDED_BY(pending_mutex_) = true;
  bool processing_in_progress_ FXL_GUARDED_BY(pending_mutex_) = false;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PACKET_QUEUE_H_
