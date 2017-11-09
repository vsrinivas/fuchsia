// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_link_packet_source.h"

#include "garnet/bin/media/audio_server/audio_object.h"
#include "garnet/bin/media/audio_server/audio_renderer_format_info.h"
#include "garnet/bin/media/audio_server/audio_renderer_impl.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

AudioLinkPacketSource::AudioLinkPacketSource(
    fbl::RefPtr<AudioObject> source,
    fbl::RefPtr<AudioObject> dest,
    fbl::RefPtr<AudioRendererFormatInfo> format_info)
    : AudioLink(SourceType::Packet, std::move(source), std::move(dest)),
      format_info_(std::move(format_info)),
      pending_queue_(new PacketQueue) {}

AudioLinkPacketSource::~AudioLinkPacketSource() {
  ReleaseQueue(pending_queue_);
}

// static
std::shared_ptr<AudioLinkPacketSource> AudioLinkPacketSource::Create(
    fbl::RefPtr<AudioObject> source,
    fbl::RefPtr<AudioObject> dest) {
  FXL_DCHECK(source);
  FXL_DCHECK(dest);

  // TODO(johngro): Relax this if we get to the point where other audio objects
  // may also be packet sources.
  if (source->type() != AudioObject::Type::Renderer) {
    FXL_LOG(ERROR)
        << "Cannot create packet source link, packet sources must be renderers";
    return nullptr;
  }

  auto& renderer = *(static_cast<AudioRendererImpl*>(source.get()));

  FXL_DCHECK(renderer.format_info_valid());
  return std::shared_ptr<AudioLinkPacketSource>(new AudioLinkPacketSource(
      std::move(source), std::move(dest), renderer.format_info()));
}

void AudioLinkPacketSource::PushToPendingQueue(
    const AudioPipe::AudioPacketRefPtr& pkt) {
  fxl::MutexLocker locker(&pending_queue_mutex_);
  pending_queue_->emplace_back(pkt);
}

void AudioLinkPacketSource::FlushPendingQueue() {
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
    fxl::MutexLocker locker(&flush_mutex_);
    {
      // TODO(johngro): Assuming that it is impossible to push a new packet
      // while a flush is in progress, it's pretty easy to show that this lock
      // can never be contended.  Because of this, we could consider removing
      // this lock operation (although, flush is a relatively rare operation, so
      // the extra overhead is pretty insignificant.
      fxl::MutexLocker locker(&pending_queue_mutex_);
      pending_queue_.swap(new_queue);
    }
    flushed_ = true;
  }

  ReleaseQueue(new_queue);
}

void AudioLinkPacketSource::CopyPendingQueue(
    const std::shared_ptr<AudioLinkPacketSource>& other) {
  FXL_DCHECK(other != nullptr);
  FXL_DCHECK(this != other.get());

  fxl::MutexLocker source_locker(&other->pending_queue_mutex_);
  if (other->pending_queue_->empty())
    return;

  fxl::MutexLocker locker(&pending_queue_mutex_);
  FXL_DCHECK(pending_queue_->empty());
  *pending_queue_ = *other->pending_queue_;
}

AudioPipe::AudioPacketRefPtr AudioLinkPacketSource::LockPendingQueueFront(
    bool* was_flushed) {
  flush_mutex_.Lock();

  FXL_DCHECK(was_flushed);
  *was_flushed = flushed_;
  flushed_ = false;

  {
    fxl::MutexLocker locker(&pending_queue_mutex_);
    if (pending_queue_->size()) {
      return pending_queue_->front();
    } else {
      return nullptr;
    }
  }
}

void AudioLinkPacketSource::UnlockPendingQueueFront(
    AudioPipe::AudioPacketRefPtr* pkt,
    bool release_packet) {
  {
    fxl::MutexLocker locker(&pending_queue_mutex_);

    // Assert that the user either got no packet when they locked the queue
    // (because the queue was empty), or that they got the front of the queue
    // and that the front of the queue has not changed.
    FXL_DCHECK(pkt);
    FXL_DCHECK((*pkt == nullptr) ||
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

void AudioLinkPacketSource::ReleaseQueue(const PacketQueuePtr& queue) {
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
