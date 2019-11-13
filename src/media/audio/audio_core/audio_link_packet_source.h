// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_PACKET_SOURCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_PACKET_SOURCE_H_

#include <deque>
#include <memory>
#include <mutex>

#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_link.h"
#include "src/media/audio/audio_core/audio_packet_ref.h"
#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/pending_flush_token.h"

namespace media::audio {

class AudioLinkPacketSource : public AudioLink {
 public:
  static fbl::RefPtr<AudioLinkPacketSource> Create(fbl::RefPtr<AudioObject> source,
                                                   fbl::RefPtr<AudioObject> dest,
                                                   fbl::RefPtr<Format> format);
  ~AudioLinkPacketSource() override;

  // Accessor for the format info assigned to this link.
  //
  // TODO(johngro): Eliminate this. Format information belongs at the generic AudioLink level.
  // Additionally, all sources should be able to to change or invalidate their format info without
  // needing to destroy and re-create any links. Ideally, they should be able to do so without
  // needing to obtain any locks. A lock-less single writer, single reader, triple-buffer object
  // would be perfect for this (I have one of these lying around from a previous project, I just
  // need to see if I am allowed to use it or not).
  const Format& format() const { return *format_; }

  // Common pending queue ops.
  bool pending_queue_empty() const {
    std::lock_guard<std::mutex> locker(pending_mutex_);
    return pending_packet_queue_.empty();
  }

  // PendingQueue operations used by the packet source. Never call these from the destination.
  void PushToPendingQueue(const fbl::RefPtr<AudioPacketRef>& packet);
  void FlushPendingQueue(const fbl::RefPtr<PendingFlushToken>& flush_token = nullptr);

  // PendingQueue operations used by the destination. Never call these from the source.
  //
  // When consuming audio, destinations must always pair their calls to LockPendingQueueFront and
  // UnlockPendingQueueFront, passing the pointer to the reference to the front of the queue they
  // obtained in the process (even if the front of the queue was nullptr).
  //
  // Doing so ensures that sources which are attempting to flush the pending queue are forced to
  // wait if the front of the queue is involved in a mixing operation. This, in turn, guarantees
  // that audio packets are always returned to the user in the order which they were queued in
  // without forcing AudioRenderers to wait to queue new data if a mix operation is in progress.
  fbl::RefPtr<AudioPacketRef> LockPendingQueueFront(bool* was_flushed);
  void UnlockPendingQueueFront(bool release_packet);

 private:
  AudioLinkPacketSource(fbl::RefPtr<AudioObject> source, fbl::RefPtr<AudioObject> dest,
                        fbl::RefPtr<Format> format);

  fbl::RefPtr<Format> format_;

  std::mutex flush_mutex_;
  mutable std::mutex pending_mutex_;

  std::deque<fbl::RefPtr<AudioPacketRef>> pending_packet_queue_ FXL_GUARDED_BY(pending_mutex_);
  std::deque<fbl::RefPtr<AudioPacketRef>> pending_flush_packet_queue_
      FXL_GUARDED_BY(pending_mutex_);
  std::deque<fbl::RefPtr<PendingFlushToken>> pending_flush_token_queue_
      FXL_GUARDED_BY(pending_mutex_);
  bool flushed_ FXL_GUARDED_BY(pending_mutex_) = true;
  bool processing_in_progress_ FXL_GUARDED_BY(pending_mutex_) = false;
};

//
// Utility function used by packet-oriented AudioObjects (e.g. AudioRenderer)
inline AudioLinkPacketSource& AsPacketSource(AudioLink& link) {
  FX_DCHECK(link.source_type() == AudioLink::SourceType::Packet);
  return static_cast<AudioLinkPacketSource&>(link);
}

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_PACKET_SOURCE_H_
