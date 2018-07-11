// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_server_impl.h"

#include <fs/service.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/bin/media/audio_server/audio_capturer_impl.h"
#include "garnet/bin/media/audio_server/audio_device_manager.h"
#include "garnet/bin/media/audio_server/audio_renderer_impl.h"

namespace media {
namespace audio {

AudioServerImpl::AudioServerImpl() : device_manager_(this) {
  // Stash a pointer to our async object.
  dispatcher_ = async_get_default_dispatcher();
  FXL_DCHECK(dispatcher_);

  // TODO(johngro) : See MG-940
  //
  // Eliminate this as soon as we have a more official way of
  // meeting real-time latency requirements.  The main async_t is
  // responsible for receiving audio payloads sent by applications, so it has
  // real time requirements (just like the mixing threads do).  In a perfect
  // world, however, we would want to have this task run on a thread which is
  // different from the thread which is processing *all* audio server jobs (even
  // non-realtime ones).  This, however, will take more significant
  // restructuring.  We will cross that bridge when we have the TBD way to deal
  // with realtime requirements in place.
  async::PostTask(
      dispatcher_, []() { zx_thread_set_priority(24 /* HIGH_PRIORITY in LK */); });

  // Set up our output manager.
  zx_status_t res = device_manager_.Init();
  // TODO(johngro): Do better at error handling than this weak check.
  FXL_DCHECK(res == ZX_OK);

  // Wait for 50 mSec before we export our services and start to process client
  // requests.  This will give the device manager layer time to discover the
  // AudioInputs and AudioOutputs which are already connected to the system.
  //
  // TODO(johngro): With some more major surgery, we could rework the device
  // manager so that we wait until we are certain that we have discovered and
  // probed the capabilities of all of the pre-existing inputs and outputs
  // before proceeding.  See MTWN-118
  async::PostDelayedTask(dispatcher_, [this]() { PublishServices(); }, zx::msec(50));
}

AudioServerImpl::~AudioServerImpl() {
  Shutdown();
  FXL_DCHECK(packet_cleanup_queue_.is_empty());
  FXL_DCHECK(flush_cleanup_queue_.is_empty());
}

void AudioServerImpl::PublishServices() {
  auto audio_service =
      fbl::AdoptRef(new fs::Service([this](zx::channel ch) -> zx_status_t {
        bindings_.AddBinding(
            this, fidl::InterfaceRequest<fuchsia::media::Audio>(std::move(ch)));
        bindings_.bindings().back()->events().SystemGainMuteChanged(
            system_gain_db_, system_muted_);
        return ZX_OK;
      }));
  outgoing_.public_dir()->AddEntry(fuchsia::media::Audio::Name_,
                                   std::move(audio_service));
  // TODO(dalesat): Load the gain/mute values.

  auto audio_device_enumerator_service =
      fbl::AdoptRef(new fs::Service([this](zx::channel ch) -> zx_status_t {
        device_manager_.AddDeviceEnumeratorClient(std::move(ch));
        return ZX_OK;
      }));
  outgoing_.public_dir()->AddEntry(fuchsia::media::AudioDeviceEnumerator::Name_,
                                   std::move(audio_device_enumerator_service));

  outgoing_.ServeFromStartupInfo();
}

void AudioServerImpl::Shutdown() {
  shutting_down_ = true;
  device_manager_.Shutdown();
  DoPacketCleanup();
}

void AudioServerImpl::CreateRendererV2(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer2> audio_renderer) {
  device_manager_.AddRenderer(
      AudioRendererImpl::Create(std::move(audio_renderer), this));
}

void AudioServerImpl::CreateCapturer(
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer>
        audio_capturer_request,
    bool loopback) {
  device_manager_.AddCapturer(AudioCapturerImpl::Create(
      std::move(audio_capturer_request), this, loopback));
}

void AudioServerImpl::SetSystemGain(float db_gain) {
  db_gain = std::max(std::min(db_gain, kMaxSystemAudioGain),
                     fuchsia::media::kMutedGain);

  if (system_gain_db_ == db_gain) {
    return;
  }

  system_gain_db_ = db_gain;

  device_manager_.OnSystemGainChanged();
  NotifyGainMuteChanged();
}

void AudioServerImpl::SetSystemMute(bool muted) {
  if (system_muted_ == muted) {
    return;
  }

  system_muted_ = muted;

  device_manager_.OnSystemGainChanged();
  NotifyGainMuteChanged();
}

void AudioServerImpl::NotifyGainMuteChanged() {
  for (auto& binding : bindings_.bindings()) {
    binding->events().SystemGainMuteChanged(system_gain_db_, system_muted_);
  }

  // TODO(dalesat): Save the gain/mute values.
}

void AudioServerImpl::SetRoutingPolicy(
    fuchsia::media::AudioOutputRoutingPolicy policy) {
  device_manager_.SetRoutingPolicy(policy);
}

void AudioServerImpl::DoPacketCleanup() {
  // In order to minimize the time we spend in the lock we obtain the lock, swap
  // the contents of the cleanup queue with a local queue and clear the sched
  // flag, and finally unlock clean out the queue (which has the side effect of
  // triggering all of the send packet callbacks).
  //
  // Note: this is only safe because we know that we are executing on a single
  // threaded task runner.  Without this guarantee, it might be possible call
  // the send packet callbacks in a different order than the packets were sent
  // in the first place.  If the async object for the audio server ever loses
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

void AudioServerImpl::SchedulePacketCleanup(
    fbl::unique_ptr<AudioPacketRef> packet) {
  std::lock_guard<std::mutex> locker(cleanup_queue_mutex_);

  packet_cleanup_queue_.push_back(std::move(packet));

  if (!cleanup_scheduled_ && !shutting_down_) {
    FXL_DCHECK(dispatcher_);
    async::PostTask(dispatcher_, [this]() { DoPacketCleanup(); });
    cleanup_scheduled_ = true;
  }
}

void AudioServerImpl::ScheduleFlushCleanup(
    fbl::unique_ptr<PendingFlushToken> token) {
  std::lock_guard<std::mutex> locker(cleanup_queue_mutex_);

  flush_cleanup_queue_.push_back(std::move(token));

  if (!cleanup_scheduled_ && !shutting_down_) {
    FXL_DCHECK(dispatcher_);
    async::PostTask(dispatcher_, [this]() { DoPacketCleanup(); });
    cleanup_scheduled_ = true;
  }
}

}  // namespace audio
}  // namespace media
