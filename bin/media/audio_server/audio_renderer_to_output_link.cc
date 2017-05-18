// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/audio_renderer_to_output_link.h"

#include "apps/media/src/audio_server/audio_output.h"
#include "apps/media/src/audio_server/audio_renderer_impl.h"
#include "lib/ftl/logging.h"

namespace media {
namespace audio {

AudioRendererToOutputLink::Bookkeeping::~Bookkeeping() {}

AudioRendererToOutputLink::AudioRendererToOutputLink(
    AudioRendererImplWeakPtr renderer,
    AudioOutputWeakPtr output)
    : renderer_(renderer), output_(output), pending_queue_(new PacketQueue) {}

AudioRendererToOutputLink::~AudioRendererToOutputLink() {
  ReleaseQueue(pending_queue_);
}

void AudioRendererToOutputLink::UpdateGain() {
  AudioRendererImplPtr renderer(GetRenderer());
  AudioOutputPtr output(GetOutput());

  // If either side of this relationship is going away, then we are shutting
  // down.  Don't bother to re-calculate the amplitude scale factor.
  if (!renderer || !output) {
    return;
  }

  // Obtain the renderer gain and, if it is at or below the muted threshold,
  // force the renderer to be muted and get out.
  double db_gain = renderer->DbGain();
  if (db_gain <= AudioRenderer::kMutedGain) {
    gain_.ForceMute();
    return;
  }

  // Add in the output gain and clamp to the maximum allowed total gain.
  db_gain += output->DbGain();
  if (db_gain > AudioRenderer::kMaxGain) {
    db_gain = AudioRenderer::kMaxGain;
  }

  gain_.Set(db_gain);
}

AudioRendererToOutputLinkPtr AudioRendererToOutputLink::New(
    AudioRendererImplWeakPtr renderer,
    AudioOutputWeakPtr output) {
  return AudioRendererToOutputLinkPtr(
      new AudioRendererToOutputLink(renderer, output));
}

void AudioRendererToOutputLink::PushToPendingQueue(
    const AudioPipe::AudioPacketRefPtr& pkt) {
  ftl::MutexLocker locker(&pending_queue_mutex_);
  pending_queue_->emplace_back(pkt);
}

void AudioRendererToOutputLink::FlushPendingQueue() {
  // Create a new (empty) queue before obtaining any locks.  This will allow us
  // to quickly swap the empty queue for the current queue and get out of all
  // the locks, and then release the packets at our leisure instead of
  // potentially holding off a high priority mixing thread while releasing
  // packets.
  //
  // Note: the safety of this technique depends on Flush only ever being called
  // from the AudioRenderer, and the AudioRenderer's actions being serialized on
  // the AudioServer's message loop thread.  If multiple flushes are allowed to
  // be invoked simultaneously, or if a packet is permitted to be added to the
  // queue while a flush operation is in progress, it is possible to return
  // packets to the user in an order different than the one that they were
  // queued in.
  PacketQueuePtr new_queue(new PacketQueue);

  {
    ftl::MutexLocker locker(&flush_mutex_);
    {
      // TODO(johngro): Assuming that it is impossible to push a new packet
      // while a flush is in progress, it's pretty easy to show that this lock
      // can never be contended.  Because of this, we could consider removing
      // this lock operation (although, flush is a relatively rare operation, so
      // the extra overhead is pretty insignificant.
      ftl::MutexLocker locker(&pending_queue_mutex_);
      pending_queue_.swap(new_queue);
    }
    flushed_ = true;
  }

  ReleaseQueue(new_queue);
}

void AudioRendererToOutputLink::InitPendingQueue(
    const AudioRendererToOutputLinkPtr& source) {
  FTL_DCHECK(source != nullptr);
  FTL_DCHECK(this != source.get());

  ftl::MutexLocker source_locker(&source->pending_queue_mutex_);
  if (source->pending_queue_->empty()) return;

  ftl::MutexLocker locker(&pending_queue_mutex_);
  FTL_DCHECK(pending_queue_->empty());
  *pending_queue_ = *source->pending_queue_;
}

AudioPipe::AudioPacketRefPtr AudioRendererToOutputLink::LockPendingQueueFront(
    bool* was_flushed) {
  flush_mutex_.Lock();

  FTL_DCHECK(was_flushed);
  *was_flushed = flushed_;
  flushed_ = false;

  {
    ftl::MutexLocker locker(&pending_queue_mutex_);
    if (pending_queue_->size()) {
      return pending_queue_->front();
    } else {
      return nullptr;
    }
  }
}

void AudioRendererToOutputLink::UnlockPendingQueueFront(
    AudioPipe::AudioPacketRefPtr* pkt,
    bool release_packet) {
  {
    ftl::MutexLocker locker(&pending_queue_mutex_);

    // Assert that the user either got no packet when they locked the queue
    // (because the queue was empty), or that they got the front of the queue
    // and that the front of the queue has not changed.
    FTL_DCHECK(pkt);
    FTL_DCHECK((*pkt == nullptr) ||
               (pending_queue_->size() && (*pkt == pending_queue_->front())));

    if (*pkt) {
      *pkt = nullptr;
      if (release_packet) {
        pending_queue_->pop_front();
      }
    }
  }

  flush_mutex_.Unlock();
}

void AudioRendererToOutputLink::ReleaseQueue(const PacketQueuePtr& queue) {
  if (!queue) {
    return;
  }

  for (auto iter = queue->begin(); iter != queue->end(); ++iter) {
    (*iter).reset();
  }

  queue->clear();
}

}  // namespace audio
}  // namespace media
