// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/unique_ptr.h>

#include "garnet/bin/media/audio_server/audio_device_manager.h"
#include "garnet/bin/media/audio_server/audio_packet_ref.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "garnet/bin/media/audio_server/pending_flush_token.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/audio_capturer.fidl.h"
#include "lib/media/fidl/audio_renderer.fidl.h"
#include "lib/media/fidl/audio_server.fidl.h"

namespace media {
namespace audio {

class AudioServerImpl : public AudioServer {
 public:
  AudioServerImpl(std::unique_ptr<app::ApplicationContext> application_context);
  ~AudioServerImpl() override;

  // AudioServer
  // TODO(mpuryear): through the codebase, particularly in examples and headers,
  // change 'audio_renderer' variables to 'audio_renderer_request' (media, etc).
  void CreateRenderer(
      f1dl::InterfaceRequest<AudioRenderer> audio_renderer,
      f1dl::InterfaceRequest<MediaRenderer> media_renderer) final;
  void CreateRendererV2(
      f1dl::InterfaceRequest<AudioRenderer2> audio_renderer) final;
  void CreateCapturer(
      f1dl::InterfaceRequest<AudioCapturer> audio_capturer_request,
      bool loopback) final;
  void SetMasterGain(float db_gain) final;
  void GetMasterGain(const GetMasterGainCallback& cbk) final;

  // Called (indirectly) by AudioOutputs to schedule the callback for a
  // packet was queued to an AudioRenderer.
  //
  // TODO(johngro): This bouncing through thread contexts is inefficient and
  // will increase the latency requirements for clients (its going to take them
  // some extra time to discover that their media has been completely consumed).
  // When fidl exposes a way to safely invoke interface method callbacks from
  // threads other than the thread which executed the method itself, we will
  // want to switch to creating the callback message directly, instead of
  // indirecting through the server.
  void SchedulePacketCleanup(fbl::unique_ptr<AudioPacketRef> packet);
  void ScheduleFlushCleanup(fbl::unique_ptr<PendingFlushToken> token);

  // Schedule a closure to run on the server's main message loop.
  void ScheduleMessageLoopTask(const fxl::Closure& task) {
    FXL_DCHECK(task_runner_);
    task_runner_->PostTask(task);
  }

  // Accessor for our encapsulated device manager.
  AudioDeviceManager& GetDeviceManager() { return device_manager_; }

 private:
  void Shutdown();
  void DoPacketCleanup();

  std::unique_ptr<app::ApplicationContext> application_context_;
  f1dl::BindingSet<AudioServer> bindings_;

  // A reference to our message loop's task runner.  Allows us to post events to
  // be handled by our main application thread from things like the output
  // manager's thread pool.
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // State for dealing with devices.
  AudioDeviceManager device_manager_;

  // State for dealing with cleanup tasks.
  fxl::Mutex cleanup_queue_mutex_;
  fbl::DoublyLinkedList<fbl::unique_ptr<AudioPacketRef>> packet_cleanup_queue_
      FXL_GUARDED_BY(cleanup_queue_mutex_);
  fbl::DoublyLinkedList<fbl::unique_ptr<PendingFlushToken>> flush_cleanup_queue_
      FXL_GUARDED_BY(cleanup_queue_mutex_);
  bool cleanup_scheduled_ FXL_GUARDED_BY(cleanup_queue_mutex_) = false;
  bool shutting_down_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(AudioServerImpl);
};

}  // namespace audio
}  // namespace media
