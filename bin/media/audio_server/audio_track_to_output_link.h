// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <memory>

#include "apps/media/src/audio_server/audio_pipe.h"
#include "apps/media/src/audio_server/fwd_decls.h"
#include "apps/media/src/audio_server/gain.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace mojo {
namespace media {
namespace audio {

// AudioTrackToOutputLink is a small class which tracks the relationship between
// an audio track and an audio output.  Tracks and outputs are expected to hold
// strong pointers to the state in the collections they use to track their
// peers.
//
// When either a track or an output ceases to exist, its collection will clear
// releasing the reference to the shared state.  When the other half of the
// relationship realizes that its peer has gone away (typically by failing to
// promote the weak reference to its peer held in the share state object), it
// can purge the state object strong pointer from its collection triggering the
// final cleanup of the shared state.
//
// Because the final cleanup of the shared state can be triggered either from an
// output manager mixer thread, or from the audio service's main message loop,
// it must be safe to destruct all of the shared state from any thread in the
// system.  No assumptions may be made about threading when destructing.
//
// The AudioTrackToOutputLink object holds a queue of pending audio packet
// references sourced from the AudioTrack to be rendered on the audio output.
// The references are safe to release either from an output manager thread, or
// from the audio service's main message loop thread (which drives track
// behavior).
//
// Finally, both the Output may have a pointer to a Bookkeeping object in order
// to manage bookkeeping tasks specific to the Track/Output relationship.  The
// following rules must be obeyed at all times...
//
// + Derrived classes of the Bookkeeping object created by the Output must be
//   safe to destroy either thread.  During destruction, no potentially blocking
//   operations may be performed.  No heavy operations (such as logging) should
//   be performed.
// + Only the output is permitted to access the output bookkeeping.  The track
//   must make no attempts to modify the bookkeeping or its pointer.
// + Outputs must hold a strong reference to the shared link object object
//   whenever they are accessing their bookkeeping object.  The link object is
//   considered to be the owner of the Bookkeeping, users must never hold a
//   naked pointer to their bookkeeping if the link could possibly destruct.
//
class AudioTrackToOutputLink {
 public:
  struct Bookkeeping {
    virtual ~Bookkeeping();
  };

  using BookkeepingPtr = std::unique_ptr<Bookkeeping>;
  using PacketQueue = std::deque<AudioPipe::AudioPacketRefPtr>;
  using PacketQueuePtr = std::unique_ptr<PacketQueue>;

  static AudioTrackToOutputLinkPtr New(AudioTrackImplWeakPtr track,
                                       AudioOutputWeakPtr output);
  virtual ~AudioTrackToOutputLink();

  // Utility function which recomputes the amplitude scale factor as function of
  // the track and the output gains.  Should only be called from the audio
  // service's main message loop thread.
  void UpdateGain();

  // Accessor for the current value of the gain's amplitude scalar.
  Gain::AScale amplitude_scale() const { return gain_.amplitude_scale(); }

  // Accessors for the track and output pointers.  Automatically attempts to
  // promote the weak pointer to a strong pointer.
  //
  // TODO(johngro):  Given the way outputs are currently shut down, there is
  // actually no need for the link to hold a weak pointer to output.  By the
  // time it destructs, All references to it are guaranteed to have been removed
  // from all tracks in the context of the main event loop.  Consider converting
  // this from a weak pointer to a strong pointer.
  AudioTrackImplPtr GetTrack() { return track_.lock(); }
  AudioOutputPtr GetOutput() { return output_.lock(); }

  // AudioTrack PendingQueue operations.  Never call these from the AudioOutput.
  void PushToPendingQueue(const AudioPipe::AudioPacketRefPtr& pkt);
  void FlushPendingQueue();

  // AudioOutput PendingQueue operations.  Never call these from the AudioTrack.
  // When consuming audio, AudioOutputs must always pair their calls to
  // LockPendingQueueFront and UnlockPendingQueueFront, passing the pointer to
  // the reference to the front of the queue they obtained in the process (even
  // if the front of the queue was nullptr).
  //
  // Doing so ensures that AudioTracks which are attempting to flush the pending
  // queue are forced to wait if the front of the queue is involved in a mixing
  // operation.  This, in turn, guarantees that audio packets are always
  // returned to the user in the order which they were queued in without forcing
  // AudioTracks to wait to queue new data if a mix operation is in progress.
  AudioPipe::AudioPacketRefPtr LockPendingQueueFront(bool* was_flushed)
      FTL_ACQUIRE(flush_mutex_);
  void UnlockPendingQueueFront(AudioPipe::AudioPacketRefPtr* pkt,
                               bool release_packet)
      FTL_THREAD_ANNOTATION_ATTRIBUTE__(release_capability(flush_mutex_));

  // Bookkeeping access.
  //
  BookkeepingPtr& output_bookkeeping() { return output_bookkeeping_; }

 private:
  void ReleaseQueue(const PacketQueuePtr& queue);

  AudioTrackToOutputLink(AudioTrackImplWeakPtr track,
                         AudioOutputWeakPtr output);

  AudioTrackImplWeakPtr track_;
  AudioOutputWeakPtr output_;
  BookkeepingPtr output_bookkeeping_;

  ftl::Mutex flush_mutex_;
  ftl::Mutex pending_queue_mutex_;
  PacketQueuePtr pending_queue_ FTL_GUARDED_BY(pending_queue_mutex_);
  bool flushed_ FTL_GUARDED_BY(flush_mutex_) = true;
  Gain gain_;
};

}  // namespace audio
}  // namespace media
}  // namespace mojo
