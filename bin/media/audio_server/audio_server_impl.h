// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <set>

#include "apps/media/services/audio_server.fidl.h"
#include "apps/media/services/audio_track.fidl.h"
#include "apps/media/src/audio_server/audio_output_manager.h"
#include "apps/media/src/audio_server/fwd_decls.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"
#include "lib/ftl/tasks/task_runner.h"

namespace media {
namespace audio {

class AudioServerImpl : public AudioServer {
 public:
  AudioServerImpl();
  ~AudioServerImpl() override;

  void Initialize();

  // AudioServer
  void CreateTrack(fidl::InterfaceRequest<AudioTrack> track,
                   fidl::InterfaceRequest<MediaRenderer> renderer) override;

  // Called (indirectly) by AudioOutputs to schedule the callback for a
  // MediaPacked which was queued to an AudioTrack via. a media pipe.
  //
  // TODO(johngro): This bouncing through thread contexts is inefficient and
  // will increase the latency requirements for clients (its going to take them
  // some extra time to discover that their media has been completely consumed).
  // When fidl exposes a way to safely invoke interface method callbacks from
  // threads other than the thread which executed the method itself, we will
  // want to switch to creating the callback message directly, instead of
  // indirecting through the server.
  void SchedulePacketCleanup(
      std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket> supplied_packet);

  // Schedule a closure to run on the server's main message loop.
  void ScheduleMessageLoopTask(const ftl::Closure& task) {
    FTL_DCHECK(task_runner_);
    task_runner_->PostTask(task);
  }

  // Removes a track from the set of active tracks.
  void RemoveTrack(AudioTrackImplPtr track) {
    size_t removed;
    removed = tracks_.erase(track);
    FTL_DCHECK(removed);
  }

  // Accessor for our encapsulated output manager.
  AudioOutputManager& GetOutputManager() { return output_manager_; }

 private:
  using CleanupQueue =
      std::list<std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket>>;

  void Shutdown();
  void DoPacketCleanup();

  std::unique_ptr<modular::ApplicationContext> application_context_;
  fidl::BindingSet<AudioServer> bindings_;

  // A reference to our message loop's task runner.  Allows us to post events to
  // be handled by our main application thread from things like the output
  // manager's thread pool.
  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  // State for dealing with outputs.
  AudioOutputManager output_manager_;

  // State for dealing with tracks.
  std::set<AudioTrackImplPtr> tracks_;

  // State for dealing with cleanup tasks.
  ftl::Mutex cleanup_queue_mutex_;
  std::unique_ptr<CleanupQueue> cleanup_queue_
      FTL_GUARDED_BY(cleanup_queue_mutex_);
  bool cleanup_scheduled_ FTL_GUARDED_BY(cleanup_queue_mutex_) = false;
  bool shutting_down_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(AudioServerImpl);
};

}  // namespace audio
}  // namespace media
