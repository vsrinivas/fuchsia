// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/drivers/virtual_audio/virtual_audio_stream.h"

#include <lib/affine/transform.h>
#include <lib/ddk/debug.h>
#include <lib/zx/clock.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cmath>

namespace virtual_audio {

// static
fbl::RefPtr<VirtualAudioStream> VirtualAudioStream::Create(
    const VirtualAudioDeviceImpl::Config& cfg, std::weak_ptr<VirtualAudioDeviceImpl> owner,
    zx_device_t* devnode) {
  return audio::SimpleAudioStream::Create<VirtualAudioStream>(cfg, owner, devnode);
}

void VirtualAudioStream::PostToDispatcher(fit::closure task_to_post) {
  async::PostTask(dispatcher(), std::move(task_to_post));
}

zx::time VirtualAudioStream::MonoTimeFromRefTime(const zx::clock& clock, zx::time ref_time) {
  zx_clock_details_v1_t clock_details;
  zx_status_t status = clock.get_details(&clock_details);
  ZX_ASSERT_MSG(status == ZX_OK, "Could not get_details on this clock");

  zx_time_t mono_time = affine::Transform::ApplyInverse(
      clock_details.mono_to_synthetic.reference_offset,
      clock_details.mono_to_synthetic.synthetic_offset,
      affine::Ratio(clock_details.mono_to_synthetic.rate.synthetic_ticks,
                    clock_details.mono_to_synthetic.rate.reference_ticks),
      ref_time.get());

  return zx::time{mono_time};
}

zx_status_t VirtualAudioStream::Init() {
  ZX_ASSERT_MSG(strlcpy(device_name_, config_.device_name.c_str(), sizeof(device_name_)),
                "strlcpy(device_name) failed");
  ZX_ASSERT_MSG(strlcpy(mfr_name_, config_.manufacturer_name.c_str(), sizeof(mfr_name_)),
                "strlcpy(mfr_name) failed");
  ZX_ASSERT_MSG(strlcpy(prod_name_, config_.product_name.c_str(), sizeof(prod_name_)),
                "strlcpy(prod_name) failed");

  memcpy(unique_id_.data, config_.unique_id.data(), sizeof(unique_id_.data));

  supported_formats_.reset();
  for (auto range : config_.supported_formats) {
    SimpleAudioStream::SupportedFormat format = {};
    format.range = range;
    supported_formats_.push_back(std::move(format));
  }

  fifo_depth_ = config_.fifo_depth_bytes;
  external_delay_nsec_ = config_.external_delay.to_nsecs();

  clock_domain_ = config_.clock.domain;
  clock_rate_adjustment_ = config_.clock.initial_rate_adjustment_ppm;
  zx_status_t status = EstablishReferenceClock();
  ZX_ASSERT_MSG(status == ZX_OK, "EstablishReferenceClock failed: %d", status);
  ZX_ASSERT_MSG(config_.ring_buffer.min_frames, "ring buffer min_frames cannot be zero");
  ZX_ASSERT_MSG(config_.ring_buffer.min_frames <= config_.ring_buffer.max_frames,
                "ring buffer min_frames cannot exceed max_frames");
  max_buffer_frames_ = config_.ring_buffer.max_frames;
  min_buffer_frames_ = config_.ring_buffer.min_frames;
  modulo_buffer_frames_ = config_.ring_buffer.modulo_frames;

  cur_gain_state_ = {
      .cur_mute = config_.gain.current_mute,
      .cur_agc = config_.gain.current_agc,
      .cur_gain = config_.gain.current_gain_db,
      .can_mute = config_.gain.can_mute,
      .can_agc = config_.gain.can_agc,
      .min_gain = config_.gain.min_gain_db,
      .max_gain = config_.gain.max_gain_db,
      .gain_step = config_.gain.gain_step_db,
  };

  audio_pd_notify_flags_t plug_flags = 0;
  if (config_.plug.hardwired) {
    plug_flags |= AUDIO_PDNF_HARDWIRED;
  }
  if (config_.plug.can_notify) {
    plug_flags |= AUDIO_PDNF_CAN_NOTIFY;
  }
  if (config_.plug.plugged) {
    plug_flags |= AUDIO_PDNF_PLUGGED;
  }
  SetInitialPlugState(plug_flags);

  if (config_.initial_notifications_per_ring.has_value()) {
    va_client_notifications_per_ring_ = config_.initial_notifications_per_ring.value();
  }

  return ZX_OK;
}

// We use this clock to emulate a real hardware time source. It is not exposed outside the driver.
zx_status_t VirtualAudioStream::EstablishReferenceClock() {
  zx_status_t status =
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS | ZX_CLOCK_OPT_AUTO_START,
                        nullptr, &reference_clock_);
  ZX_ASSERT_MSG(status == ZX_OK, "Could not create driver clock");

  if (clock_rate_adjustment_ != 0) {
    status = AdjustClockRate();
  }
  return ZX_OK;
}

// Update the internal clock object that manages our variance from the local system timebase.
zx_status_t VirtualAudioStream::AdjustClockRate() {
  zx::clock::update_args args;
  args.reset().set_rate_adjust(clock_rate_adjustment_);

  zx_status_t status = reference_clock_.update(args);
  ZX_ASSERT_MSG(status == ZX_OK, "Could not rate-adjust driver clock");

  return ZX_OK;
}

fit::result<VirtualAudioStream::ErrorT, VirtualAudioStream::CurrentFormat>
VirtualAudioStream::GetFormatForVA() {
  if (!frame_rate_) {
    zxlogf(WARNING, "%s: %p ring buffer not initialized yet", __func__, this);
    return fit::error(ErrorT::kNoRingBuffer);
  }

  return fit::ok(CurrentFormat{
      .frames_per_second = frame_rate_,
      .sample_format = sample_format_,
      .num_channels = num_channels_,
      .external_delay = config_.external_delay,
  });
}

VirtualAudioStream::CurrentGain VirtualAudioStream::GetGainForVA() {
  return {
      .mute = cur_gain_state_.cur_mute,
      .agc = cur_gain_state_.cur_agc,
      .gain_db = cur_gain_state_.cur_gain,
  };
}

fit::result<VirtualAudioStream::ErrorT, VirtualAudioStream::CurrentBuffer>
VirtualAudioStream::GetBufferForVA() {
  if (!ring_buffer_vmo_.is_valid()) {
    zxlogf(WARNING, "%s: %p ring buffer not initialized yet", __func__, this);
    return fit::error(ErrorT::kNoRingBuffer);
  }

  zx::vmo dup_vmo;
  zx_status_t status = ring_buffer_vmo_.duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &dup_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: %p ring buffer creation failed with status %d", __func__, this, status);
    return fit::error(ErrorT::kInternal);
  }

  return fit::ok(CurrentBuffer{
      .vmo = std::move(dup_vmo),
      .num_frames = num_ring_buffer_frames_,
      .notifications_per_ring = notifications_per_ring_,
  });
}

fit::result<VirtualAudioStream::ErrorT, VirtualAudioStream::CurrentPosition>
VirtualAudioStream::GetPositionForVA() {
  if (!ref_start_time_.get()) {
    zxlogf(WARNING, "%s: %p stream is not started yet", __func__, this);
    return fit::error(ErrorT::kNotStarted);
  }

  zx::time ref_now;
  zx_status_t status = reference_clock_.read(ref_now.get_address());
  ZX_ASSERT_MSG(status == ZX_OK, "reference_clock::read returned error (%d)", status);

  auto mono_now = MonoTimeFromRefTime(reference_clock_, ref_now);
  auto frames = ref_time_to_running_frame_.Apply(ref_now.get());
  ZX_ASSERT_MSG(frames >= 0, "running frames should never be negative");

  // This cast is safe unless the ring-buffer exceeds 4GB, which even at max bit-rate (8-channel,
  // float32 format, 192kHz) would be a 700-second ring buffer (!).
  auto ring_position = static_cast<uint32_t>((frames % num_ring_buffer_frames_) * frame_size_);

  return fit::ok(CurrentPosition{
      .monotonic_time = mono_now,
      .ring_position = ring_position,
  });
}

void VirtualAudioStream::SetNotificationFrequencyFromVA(uint32_t notifications_per_ring) {
  va_client_notifications_per_ring_ = notifications_per_ring;

  // If our client requested the same notification cadence that AudioCore did, then just use the
  // "official" AudioCore notification timer and frequency instead of this alternate mechanism.
  if (va_client_notifications_per_ring_.value() == notifications_per_ring_) {
    va_client_notifications_per_ring_ = std::nullopt;
  }
  SetVaClientNotificationPeriods();

  if (va_client_notifications_per_ring_.has_value() &&
      (va_client_notifications_per_ring_.value() > 0)) {
    zx_status_t status =
        reference_clock_.read(target_va_client_ref_notification_time_.get_address());
    ZX_ASSERT_MSG(status == ZX_OK, "reference_clock::read returned error (%d)", status);

    PostForVaClientNotifyAt(target_va_client_ref_notification_time_);
  } else {
    target_va_client_mono_notification_time_ = zx::time(0);
    va_client_notify_timer_.Cancel();
  }
}

void VirtualAudioStream::ChangePlugStateFromVA(bool plugged) { SetPlugState(plugged); }

void VirtualAudioStream::AdjustClockRateFromVA(int32_t ppm_from_monotonic) {
  clock_rate_adjustment_ = ppm_from_monotonic;
  ZX_ASSERT_MSG(AdjustClockRate() == ZX_OK, "AdjustClockRate failed in %s", __func__);
}

void VirtualAudioStream::PostForNotify() {
  ZX_ASSERT(notifications_per_ring_);
  ZX_ASSERT(target_mono_notification_time_.get() > 0);

  notify_timer_.PostForTime(dispatcher(), target_mono_notification_time_);
}

void VirtualAudioStream::PostForNotifyAt(zx::time ref_notification_time) {
  target_ref_notification_time_ = ref_notification_time;
  target_mono_notification_time_ =
      MonoTimeFromRefTime(reference_clock_, target_ref_notification_time_);
  PostForNotify();
}

void VirtualAudioStream::PostForVaClientNotify() {
  ZX_ASSERT(va_client_notifications_per_ring_.value() > 0);
  ZX_ASSERT(target_va_client_mono_notification_time_.get() > 0);

  va_client_notify_timer_.PostForTime(dispatcher(), target_va_client_mono_notification_time_);
}

void VirtualAudioStream::PostForVaClientNotifyAt(zx::time va_client_ref_notification_time) {
  target_va_client_ref_notification_time_ = va_client_ref_notification_time;
  target_va_client_mono_notification_time_ =
      MonoTimeFromRefTime(reference_clock_, target_va_client_ref_notification_time_);
  PostForVaClientNotify();
}

// On success, driver should return a valid VMO with appropriate permissions (READ | MAP | TRANSFER
// for input, plus WRITE for output) and report the total number of usable frames in the ring.
//
// Format must already be set: a ring buffer channel (over which this command arrived) is provided
// as the return value from a successful SetFormat call.
zx_status_t VirtualAudioStream::GetBuffer(const audio::audio_proto::RingBufGetBufferReq& req,
                                          uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  ZX_ASSERT_MSG(req.notifications_per_ring <= req.min_ring_buffer_frames,
                "req.notifications_per_ring too big");
  ZX_ASSERT_MSG(req.min_ring_buffer_frames <= max_buffer_frames_,
                "req.min_ring_buffer_frames too big");

  num_ring_buffer_frames_ = std::max(
      min_buffer_frames_,
      fbl::round_up<uint32_t, uint32_t>(req.min_ring_buffer_frames, modulo_buffer_frames_));
  // Using uint32_t (not size_t) is safe unless the ring-buffer exceeds 4GB, which even at max
  // bit-rate (8-channel, float32 format, 192kHz) would be a 700-second ring buffer (!).
  uint32_t ring_buffer_size = fbl::round_up<uint32_t, uint32_t>(
      num_ring_buffer_frames_ * frame_size_, zx_system_get_page_size());

  if (ring_buffer_mapper_.start() != nullptr) {
    ring_buffer_mapper_.Unmap();
  }

  zx_status_t status = ring_buffer_mapper_.CreateAndMap(
      ring_buffer_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &ring_buffer_vmo_,
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER);

  ZX_ASSERT_MSG(status == ZX_OK, "%s failed to create ring buffer vmo - %d", __func__, status);

  status = ring_buffer_vmo_.duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, out_buffer);
  ZX_ASSERT_MSG(status == ZX_OK, "%s failed to duplicate VMO handle for out param - %d", __func__,
                status);

  notifications_per_ring_ = req.notifications_per_ring;
  SetNotificationPeriods();

  *out_num_rb_frames = num_ring_buffer_frames_;

  zx::vmo duplicate_vmo;
  status = ring_buffer_vmo_.duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &duplicate_vmo);
  ZX_ASSERT_MSG(status == ZX_OK, "%s failed to duplicate VMO handle for VA client - %d", __func__,
                status);

  auto parent = parent_.lock();
  ZX_ASSERT(parent);
  parent->NotifyBufferCreated(std::move(duplicate_vmo), num_ring_buffer_frames_,
                              notifications_per_ring_);

  return ZX_OK;
}

void VirtualAudioStream::SetNotificationPeriods() {
  if (notifications_per_ring_ > 0) {
    ref_notification_period_ = zx::duration((zx::sec(1) * num_ring_buffer_frames_) /
                                            (frame_rate_ * notifications_per_ring_));
  } else {
    ref_notification_period_ = zx::duration(0);
  }

  SetVaClientNotificationPeriods();
}

void VirtualAudioStream::SetVaClientNotificationPeriods() {
  if (va_client_notifications_per_ring_.has_value() &&
      va_client_notifications_per_ring_.value() > 0) {
    va_client_ref_notification_period_ =
        zx::duration((zx::sec(1) * num_ring_buffer_frames_) /
                     (frame_rate_ * va_client_notifications_per_ring_.value()));
  } else {
    va_client_ref_notification_period_ = zx::duration(0);
  }
}

zx_status_t VirtualAudioStream::ChangeFormat(const audio::audio_proto::StreamSetFmtReq& req) {
  // frame_size_ is already set, automatically
  ZX_ASSERT_MSG(frame_size_, "frame_size canot be zero");

  frame_rate_ = req.frames_per_second;
  ZX_ASSERT_MSG(frame_rate_, "frame_rate cannot be zero");

  sample_format_ = req.sample_format;

  num_channels_ = req.channels;
  bytes_per_sec_ = frame_rate_ * frame_size_;

  // (Re)set external_delay_nsec_ and fifo_depth_ before leaving, if needed.

  auto parent = parent_.lock();
  ZX_ASSERT(parent);
  parent->NotifySetFormat(frame_rate_, sample_format_, num_channels_, external_delay_nsec_);

  return ZX_OK;
}

zx_status_t VirtualAudioStream::SetGain(const audio::audio_proto::SetGainReq& req) {
  if (req.flags & AUDIO_SGF_GAIN_VALID) {
    cur_gain_state_.cur_gain =
        trunc(req.gain / cur_gain_state_.gain_step) * cur_gain_state_.gain_step;
  }

  if (req.flags & AUDIO_SGF_MUTE_VALID) {
    cur_gain_state_.cur_mute = req.flags & AUDIO_SGF_MUTE;
  }

  if (req.flags & AUDIO_SGF_AGC_VALID) {
    cur_gain_state_.cur_agc = req.flags & AUDIO_SGF_AGC;
  }

  auto parent = parent_.lock();
  ZX_ASSERT(parent);
  parent->NotifySetGain(cur_gain_state_.cur_mute, cur_gain_state_.cur_agc,
                        cur_gain_state_.cur_gain);

  return ZX_OK;
}

// Drivers *must* report the time (on CLOCK_MONOTONIC timeline) at which the first frame will be
// clocked out, not including any external delay.
zx_status_t VirtualAudioStream::Start(uint64_t* out_start_time) {
  zx_status_t status = reference_clock_.read(ref_start_time_.get_address());
  ZX_ASSERT_MSG(status == ZX_OK, "reference_clock::read returned error (%d)", status);

  // Incorporate delay caused by fifo_depth_
  ref_start_time_ += zx::duration((zx::sec(1) * fifo_depth_) / bytes_per_sec_);

  ref_time_to_running_frame_ =
      affine::Transform(ref_start_time_.get(), 0, affine::Ratio(frame_rate_, ZX_SEC(1)));

  auto mono_start_time = MonoTimeFromRefTime(reference_clock_, ref_start_time_);

  auto parent = parent_.lock();
  ZX_ASSERT(parent);
  parent->NotifyStart(mono_start_time.get());

  // Set the timer here (if notifications are enabled).
  if (ref_notification_period_.get() > 0) {
    PostForNotifyAt(ref_start_time_);
  }

  if (va_client_ref_notification_period_.get() > 0) {
    PostForVaClientNotifyAt(ref_start_time_);
  }

  *out_start_time = mono_start_time.get();

  return ZX_OK;
}

// Timer handler for sending position notifications: to AudioCore, to VAD clients that don't set the
// notification frequency, and to VAD clients that set it to the same value that AudioCore selects.
// This method handles tasks posted to notify_timer_
void VirtualAudioStream::ProcessRingNotification() {
  audio::ScopedToken t(domain_token());
  ZX_ASSERT(ref_notification_period_.get() > 0);
  ZX_ASSERT(notifications_per_ring_ > 0);
  ZX_ASSERT(target_mono_notification_time_.get() > 0);

  zx::time ref_now;
  zx_status_t status = reference_clock_.read(ref_now.get_address());
  ZX_ASSERT_MSG(status == ZX_OK, "reference_clock::read returned error (%d)", status);

  // We should wake up close to target_ref_notification_time_
  if (target_ref_notification_time_ > ref_now) {
    PostForNotify();  // Too soon, re-post this for later
    return;
  }

  auto running_frame_position =
      ref_time_to_running_frame_.Apply(target_ref_notification_time_.get());
  ZX_ASSERT_MSG(running_frame_position >= 0, "running_frame_position should never be negative");

  auto ring_buffer_position =
      static_cast<uint32_t>((running_frame_position % num_ring_buffer_frames_) * frame_size_);
  audio::audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;
  resp.monotonic_time = target_mono_notification_time_.get();
  resp.ring_buffer_pos = ring_buffer_position;

  status = NotifyPosition(resp);
  ZX_ASSERT_MSG(status == ZX_OK, "NotifyPosition returned error (%d)", status);

  // If virtual_audio client uses this notification cadence, notify them too
  if (!va_client_notifications_per_ring_.has_value()) {
    auto parent = parent_.lock();
    ZX_ASSERT(parent);
    parent->NotifyPosition(target_mono_notification_time_.get(), ring_buffer_position);
  }

  // Post the next position notification
  PostForNotifyAt(target_ref_notification_time_ + ref_notification_period_);
}

// Handler for sending alternate position notifications: those going to VAD clients that specified
// a different notification frequency. These are not sent to AudioCore.
// This method receives tasks that have been posted to va_client_notify_timer_
void VirtualAudioStream::ProcessVaClientRingNotification() {
  audio::ScopedToken t(domain_token());
  ZX_ASSERT(va_client_ref_notification_period_.get() > 0);
  ZX_ASSERT(va_client_notifications_per_ring_.has_value());
  ZX_ASSERT(va_client_notifications_per_ring_.value() > 0);
  ZX_ASSERT(target_va_client_mono_notification_time_.get() > 0);

  zx::time ref_now;
  zx_status_t status = reference_clock_.read(ref_now.get_address());
  ZX_ASSERT_MSG(status == ZX_OK, "reference_clock::read returned error (%d)", status);

  // We should wake up close to target_ref_notification_time_
  if (target_va_client_ref_notification_time_ > ref_now) {
    PostForVaClientNotify();  // Too soon, re-post this for later
    return;
  }

  auto running_frame_position =
      ref_time_to_running_frame_.Apply(target_va_client_ref_notification_time_.get());
  ZX_ASSERT_MSG(running_frame_position >= 0, "running_frame_position should never be negative");

  auto ring_buffer_position =
      static_cast<uint32_t>((running_frame_position % num_ring_buffer_frames_) * frame_size_);

  auto parent = parent_.lock();
  ZX_ASSERT(parent);
  parent->NotifyPosition(target_va_client_mono_notification_time_.get(), ring_buffer_position);

  // Post the next alt position notification
  PostForVaClientNotifyAt(target_va_client_ref_notification_time_ +
                          va_client_ref_notification_period_);
}

zx_status_t VirtualAudioStream::Stop() {
  zx::time ref_stop_time;
  zx_status_t status = reference_clock_.read(ref_stop_time.get_address());

  notify_timer_.Cancel();
  va_client_notify_timer_.Cancel();

  ZX_ASSERT_MSG(status == ZX_OK, "reference_clock::read returned error (%d)", status);

  auto mono_stop_time = MonoTimeFromRefTime(reference_clock_, ref_stop_time);
  auto stop_frame = ref_time_to_running_frame_.Apply(ref_stop_time.get());
  ZX_ASSERT_MSG(stop_frame >= 0, "stop_frame should never be negative");

  auto ring_buf_position =
      static_cast<uint32_t>((stop_frame % num_ring_buffer_frames_) * frame_size_);

  auto parent = parent_.lock();
  ZX_ASSERT(parent);
  parent->NotifyStop(mono_stop_time.get(), ring_buf_position);

  ref_start_time_ = zx::time(0);
  target_mono_notification_time_ = zx::time(0);
  target_va_client_mono_notification_time_ = zx::time(0);
  ref_notification_period_ = zx::duration(0);
  va_client_ref_notification_period_ = zx::duration(0);

  ref_time_to_running_frame_ = affine::Transform(affine::Ratio(0, 1));

  return ZX_OK;
}

// Called by parent SimpleAudioStream::Shutdown, during DdkUnbind. Notify our parent that we are
// shutting down.
void VirtualAudioStream::ShutdownHook() {
  auto parent = parent_.lock();
  ZX_ASSERT(parent);
  parent->PostToDispatcher([parent]() { parent->StreamIsShuttingDown(); });
}

}  // namespace virtual_audio
