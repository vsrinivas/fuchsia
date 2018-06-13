// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_SERVER_IMPL_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_SERVER_IMPL_H_

#include <mutex>

#include <fbl/intrusive_double_list.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/media/audio_server/audio_device_manager.h"
#include "garnet/bin/media/audio_server/audio_packet_ref.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "garnet/bin/media/audio_server/pending_flush_token.h"
#include "lib/app/cpp/outgoing.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media {
namespace audio {

class AudioServerImpl : public fuchsia::media::Audio {
 public:
  AudioServerImpl();
  ~AudioServerImpl() override;

  // Audio implementation.
  // TODO(mpuryear): through the codebase, particularly in examples and headers,
  // change 'audio_renderer' variables to 'audio_renderer_request' (media, etc).
  void CreateRenderer(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer,
      fidl::InterfaceRequest<fuchsia::media::MediaRenderer> media_renderer)
      final;
  void CreateCapturer(fidl::InterfaceRequest<fuchsia::media::AudioCapturer>
                          audio_capturer_request,
                      bool loopback) final;

  void CreateRendererV2(fidl::InterfaceRequest<fuchsia::media::AudioRenderer2>
                            audio_renderer) final;

  void SetSystemGain(float db_gain) final;
  void SetSystemMute(bool muted) final;

  void SetRoutingPolicy(fuchsia::media::AudioOutputRoutingPolicy policy) final;

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
  void ScheduleMainThreadTask(fit::closure task) {
    FXL_DCHECK(async_);
    async::PostTask(async_, std::move(task));
  }

  // Accessor for our encapsulated device manager.
  AudioDeviceManager& GetDeviceManager() { return device_manager_; }

  float system_gain_db() const { return system_gain_db_; }
  bool system_muted() const { return system_muted_; }

 private:
  static constexpr float kDefaultSystemGainDb = -12.0f;
  static constexpr bool kDefaultSystemMuted = false;
  static constexpr float kMaxSystemAudioGain = 0.0f;

  void NotifyGainMuteChanged();
  void PublishServices();
  void Shutdown();
  void DoPacketCleanup();

  fuchsia::sys::Outgoing outgoing_;
  fidl::BindingSet<fuchsia::media::Audio> bindings_;

  // A reference to our thread's async object.  Allows us to post events to
  // be handled by our main application thread from things like the output
  // manager's thread pool.
  async_t* async_;

  // State for dealing with devices.
  AudioDeviceManager device_manager_;

  // State for dealing with cleanup tasks.
  std::mutex cleanup_queue_mutex_;
  fbl::DoublyLinkedList<fbl::unique_ptr<AudioPacketRef>> packet_cleanup_queue_
      FXL_GUARDED_BY(cleanup_queue_mutex_);
  fbl::DoublyLinkedList<fbl::unique_ptr<PendingFlushToken>> flush_cleanup_queue_
      FXL_GUARDED_BY(cleanup_queue_mutex_);
  bool cleanup_scheduled_ FXL_GUARDED_BY(cleanup_queue_mutex_) = false;
  bool shutting_down_ = false;

  // TODO(johngro): remove this state.  Move users over to using the
  // AudioDeviceEnumerator interface to control gain on a per input/output
  // basis.
  float system_gain_db_ = kDefaultSystemGainDb;
  bool system_muted_ = kDefaultSystemMuted;

  FXL_DISALLOW_COPY_AND_ASSIGN(AudioServerImpl);
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_SERVER_IMPL_H_
