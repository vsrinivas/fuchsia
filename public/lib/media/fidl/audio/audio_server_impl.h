// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_AUDIO_AUDIO_SERVER_IMPL_H_
#define APPS_MEDIA_SERVICES_AUDIO_AUDIO_SERVER_IMPL_H_

#include <list>
#include <set>

#include "apps/media/interfaces/audio_server.mojom.h"
#include "apps/media/interfaces/audio_track.mojom.h"
#include "apps/media/services/audio/audio_output_manager.h"
#include "apps/media/services/audio/fwd_decls.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/tasks/task_runner.h"

namespace mojo {
namespace media {
namespace audio {

class AudioServerImpl : public AudioServer {
 public:
  AudioServerImpl();
  ~AudioServerImpl() override;

  void Initialize();

  // AudioServer
  void CreateTrack(InterfaceRequest<AudioTrack> track,
                   InterfaceRequest<MediaRenderer> renderer) override;

  // Called (indirectly) by AudioOutputs to schedule the callback for a
  // MediaPacked which was queued to an AudioTrack via. a media pipe.
  //
  // TODO(johngro): This bouncing through thread contexts is inefficient and
  // will increase the latency requirements for clients (its going to take them
  // some extra time to discover that their media has been completely consumed).
  // When mojo exposes a way to safely invoke interface method callbacks from
  // threads other than the thread which executed the method itself, we will
  // want to switch to creating the callback message directly, instead of
  // indirecting through the server.
  void SchedulePacketCleanup(
      std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket> supplied_packet);

  // Schedule a closure to run on the server's main message loop.
  void ScheduleMessageLoopTask(const tracked_objects::Location& from_here,
                               const base::Closure& task) {
    DCHECK(task_runner_);
    bool success = task_runner_->PostTask(from_here, task);
    DCHECK(success);
  }

  // Removes a track from the set of active tracks.
  void RemoveTrack(AudioTrackImplPtr track) {
    size_t removed;
    removed = tracks_.erase(track);
    DCHECK(removed);
  }

  // Accessor for our encapsulated output manager.
  AudioOutputManager& GetOutputManager() { return output_manager_; }

 private:
  using CleanupQueue =
      std::list<std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket>>;

  void Shutdown();
  void DoPacketCleanup();

  // A reference to our message loop's task runner.  Allows us to post events to
  // be handled by our main application thread from things like the output
  // manager's thread pool.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  // State for dealing with outputs.
  AudioOutputManager output_manager_;

  // State for dealing with tracks.
  std::set<AudioTrackImplPtr> tracks_;

  // State for dealing with cleanup tasks.
  base::Lock cleanup_queue_lock_;
  std::unique_ptr<CleanupQueue> cleanup_queue_;
  bool cleanup_scheduled_ = false;
  bool shutting_down_ = false;
  base::Closure cleanup_closure_;
};

}  // namespace audio
}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_AUDIO_AUDIO_SERVER_IMPL_H_
