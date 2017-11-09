// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <deque>
#include <memory>

#include "garnet/bin/media/audio_server/audio_link.h"
#include "garnet/bin/media/audio_server/audio_pipe.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media {
namespace audio {

// TODO(johngro): docs
//
class AudioLinkPacketSource : public AudioLink {
 public:
  using PacketQueue = std::deque<AudioPipe::AudioPacketRefPtr>;
  using PacketQueuePtr = std::unique_ptr<PacketQueue>;

  static std::shared_ptr<AudioLinkPacketSource> Create(
      fbl::RefPtr<AudioObject> source,
      fbl::RefPtr<AudioObject> dest);
  ~AudioLinkPacketSource() override;

  // Accessor for the format info assigned to this link.
  //
  // TODO(johngro): Eliminate this.  Format information belongs at the generic
  // AudioLink level.  Additionally, all sources should be able to to change or
  // invalidate their format info without needing to destroy and re-create any
  // links.  Ideally, they should be able to do so without needing to obtain any
  // locks.  A lock-less single writer, single reader, triple-buffer object
  // would be perfect for this (I have one of these lying around from a previous
  // project, I just need to see if I am allowed to use it or not).
  const AudioRendererFormatInfo& format_info() const { return *format_info_; }

  // Common pending queue ops.
  bool pending_queue_empty() const {
    fxl::MutexLocker locker(&pending_queue_mutex_);
    return pending_queue_->empty();
  }

  // PendingQueue operations used by the packet source.  Never call these from
  // the destination.
  void PushToPendingQueue(const AudioPipe::AudioPacketRefPtr& pkt);
  void FlushPendingQueue();
  void CopyPendingQueue(const std::shared_ptr<AudioLinkPacketSource>& other);

  // PendingQueue operations used by the destination.  Never call these from the
  // source.
  //
  // When consuming audio, destinations must always pair their calls to
  // LockPendingQueueFront and UnlockPendingQueueFront, passing the pointer to
  // the reference to the front of the queue they obtained in the process (even
  // if the front of the queue was nullptr).
  //
  // Doing so ensures that sources which are attempting to flush the pending
  // queue are forced to wait if the front of the queue is involved in a mixing
  // operation.  This, in turn, guarantees that audio packets are always
  // returned to the user in the order which they were queued in without forcing
  // AudioRenderers to wait to queue new data if a mix operation is in progress.
  AudioPipe::AudioPacketRefPtr LockPendingQueueFront(bool* was_flushed)
      FXL_ACQUIRE(flush_mutex_);
  void UnlockPendingQueueFront(AudioPipe::AudioPacketRefPtr* pkt,
                               bool release_packet)
      FXL_THREAD_ANNOTATION_ATTRIBUTE__(release_capability(flush_mutex_));

 private:
  void ReleaseQueue(const PacketQueuePtr& queue);

  AudioLinkPacketSource(fbl::RefPtr<AudioObject> source,
                        fbl::RefPtr<AudioObject> dest,
                        fbl::RefPtr<AudioRendererFormatInfo> format_info);

  fbl::RefPtr<AudioRendererFormatInfo> format_info_;

  fxl::Mutex flush_mutex_;
  mutable fxl::Mutex pending_queue_mutex_;
  PacketQueuePtr pending_queue_ FXL_GUARDED_BY(pending_queue_mutex_);
  bool flushed_ FXL_GUARDED_BY(flush_mutex_) = true;
};

}  // namespace audio
}  // namespace media
