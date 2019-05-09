// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_core_impl.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/media/audio/audio_core/audio_capturer_impl.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/audio_renderer_impl.h"

namespace media::audio {

constexpr float AudioCoreImpl::kMaxSystemAudioGainDb;

AudioCoreImpl::AudioCoreImpl(
    std::unique_ptr<sys::ComponentContext> startup_context)
    : device_manager_(this), ctx_(std::move(startup_context)) {
  // Stash a pointer to our async object.
  dispatcher_ = async_get_default_dispatcher();
  FXL_DCHECK(dispatcher_);

  // TODO(johngro) : See ZX-940
  //
  // Eliminate this as soon as we have a more official way of
  // meeting real-time latency requirements.  The main async_t is
  // responsible for receiving audio payloads sent by applications, so it has
  // real time requirements (just like the mixing threads do).  In a perfect
  // world, however, we would want to have this task run on a thread which is
  // different from the thread which is processing *all* audio service jobs
  // (even non-realtime ones).  This, however, will take more significant
  // restructuring.  We will cross that bridge when we have the TBD way to deal
  // with realtime requirements in place.
  auto profile_provider =
      ctx_->svc()->Connect<fuchsia::scheduler::ProfileProvider>();
  profile_provider->GetProfile(
      24 /* HIGH_PRIORITY in LK */,
      "src/media/audio/audio_core/audio_core_impl",
      [](zx_status_t fidl_status, zx::profile profile) {
        FXL_DCHECK(fidl_status == ZX_OK);
        if (fidl_status == ZX_OK) {
          zx_status_t status =
              zx_object_set_profile(zx_thread_self(), profile.get(), 0);
          FXL_DCHECK(status == ZX_OK);
        }
      });

  // Set up our output manager.
  zx_status_t res = device_manager_.Init();
  // TODO(johngro): Do better at error handling than this weak check.
  FXL_DCHECK(res == ZX_OK);

  PublishServices();
}

AudioCoreImpl::~AudioCoreImpl() {
  Shutdown();
  FXL_DCHECK(packet_cleanup_queue_.is_empty());
  FXL_DCHECK(flush_cleanup_queue_.is_empty());
}

void AudioCoreImpl::PublishServices() {
  ctx_->outgoing()->AddPublicService<fuchsia::media::AudioCore>(
      [this](fidl::InterfaceRequest<fuchsia::media::AudioCore> request) {
        bindings_.AddBinding(this, std::move(request));
        bindings_.bindings().back()->events().SystemGainMuteChanged(
            system_gain_db_, system_muted_);
      });
  // TODO(dalesat): Load the gain/mute values.

  ctx_->outgoing()->AddPublicService<fuchsia::media::AudioDeviceEnumerator>(
      [this](fidl::InterfaceRequest<fuchsia::media::AudioDeviceEnumerator>
                 request) {
        device_manager_.AddDeviceEnumeratorClient(std::move(request));
      });
}

void AudioCoreImpl::Shutdown() {
  shutting_down_ = true;
  device_manager_.Shutdown();
  DoPacketCleanup();
}

void AudioCoreImpl::CreateAudioRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
        audio_renderer_request) {
  device_manager_.AddAudioRenderer(
      AudioRendererImpl::Create(std::move(audio_renderer_request), this));
}

void AudioCoreImpl::CreateAudioCapturer(
    bool loopback, fidl::InterfaceRequest<fuchsia::media::AudioCapturer>
                       audio_capturer_request) {
  device_manager_.AddAudioCapturer(AudioCapturerImpl::Create(
      std::move(audio_capturer_request), this, loopback));
}

void AudioCoreImpl::SetSystemGain(float gain_db) {
  // NAN is undefined and "signless". We cannot simply clamp it into range.
  if (isnan(gain_db)) {
    FXL_LOG(ERROR) << "Invalid system gain " << gain_db
                   << " dB -- making no change";
    return;
  }
  gain_db = std::max(std::min(gain_db, kMaxSystemAudioGainDb),
                     fuchsia::media::audio::MUTED_GAIN_DB);

  if (system_gain_db_ == gain_db) {
    // This system gain is the same as the last one we broadcast.
    // A device might have received a SetDeviceGain call since we last set this.
    // Only update devices that have diverged from the System Gain/Mute values.
    device_manager_.OnSystemGain(false);
    return;
  }

  system_gain_db_ = gain_db;

  // This will be broadcast to all output devices.
  device_manager_.OnSystemGain(true);
  NotifyGainMuteChanged();
}

void AudioCoreImpl::SetSystemMute(bool muted) {
  if (system_muted_ == muted) {
    // A device might have received a SetDeviceMute call since we last set this.
    // Only update devices that have diverged from the System Gain/Mute values.
    device_manager_.OnSystemGain(false);
    return;
  }

  system_muted_ = muted;

  // This will be broadcast to all output devices.
  device_manager_.OnSystemGain(true);
  NotifyGainMuteChanged();
}

void AudioCoreImpl::NotifyGainMuteChanged() {
  for (auto& binding : bindings_.bindings()) {
    binding->events().SystemGainMuteChanged(system_gain_db_, system_muted_);
  }
}

void AudioCoreImpl::SetRoutingPolicy(
    fuchsia::media::AudioOutputRoutingPolicy policy) {
  device_manager_.SetRoutingPolicy(policy);
}

void AudioCoreImpl::EnableDeviceSettings(bool enabled) {
  device_manager_.EnableDeviceSettings(enabled);
}

void AudioCoreImpl::DoPacketCleanup() {
  // In order to minimize the time we spend in the lock we obtain the lock, swap
  // the contents of the cleanup queue with a local queue and clear the sched
  // flag, and finally unlock clean out the queue (which has the side effect of
  // triggering all of the send packet callbacks).
  //
  // Note: this is only safe because we know that we are executing on a single
  // threaded task runner.  Without this guarantee, it might be possible call
  // the send packet callbacks in a different order than the packets were sent
  // in the first place.  If the async object for the audio service ever loses
  // this serialization guarantee (because it becomes multi-threaded, for
  // example) we will need to introduce another lock (different from the cleanup
  // lock) in order to keep the cleanup tasks properly ordered while
  // guaranteeing minimal contention of the cleanup lock (which is being
  // acquired by the high priority mixing threads).
  fbl::DoublyLinkedList<fbl::unique_ptr<AudioPacketRef>> tmp_packet_queue;
  fbl::DoublyLinkedList<fbl::unique_ptr<PendingFlushToken>> tmp_token_queue;

  {
    std::lock_guard<std::mutex> locker(cleanup_queue_mutex_);
    packet_cleanup_queue_.swap(tmp_packet_queue);
    flush_cleanup_queue_.swap(tmp_token_queue);
    cleanup_scheduled_ = false;
  }

  // Call the Cleanup method for each of the packets in order, then let the tmp
  // queue go out of scope cleaning up all of the packet references.
  for (auto& packet_ref : tmp_packet_queue) {
    packet_ref.Cleanup();
  }

  for (auto& token : tmp_token_queue) {
    token.Cleanup();
  }
}

void AudioCoreImpl::SchedulePacketCleanup(
    fbl::unique_ptr<AudioPacketRef> packet) {
  std::lock_guard<std::mutex> locker(cleanup_queue_mutex_);

  packet_cleanup_queue_.push_back(std::move(packet));

  if (!cleanup_scheduled_ && !shutting_down_) {
    FXL_DCHECK(dispatcher_);
    async::PostTask(dispatcher_, [this]() { DoPacketCleanup(); });
    cleanup_scheduled_ = true;
  }
}

void AudioCoreImpl::ScheduleFlushCleanup(
    fbl::unique_ptr<PendingFlushToken> token) {
  std::lock_guard<std::mutex> locker(cleanup_queue_mutex_);

  flush_cleanup_queue_.push_back(std::move(token));

  if (!cleanup_scheduled_ && !shutting_down_) {
    FXL_DCHECK(dispatcher_);
    async::PostTask(dispatcher_, [this]() { DoPacketCleanup(); });
    cleanup_scheduled_ = true;
  }
}

}  // namespace media::audio
