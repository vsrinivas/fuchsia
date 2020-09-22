// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/drivers/virtual_audio/virtual_audio_stream.h"

#include <lib/zx/clock.h>

#include <cmath>

#include <ddk/debug.h>

#include "src/media/audio/drivers/virtual_audio/virtual_audio_device_impl.h"
#include "src/media/audio/drivers/virtual_audio/virtual_audio_stream_in.h"
#include "src/media/audio/drivers/virtual_audio/virtual_audio_stream_out.h"

namespace virtual_audio {

// static
fbl::RefPtr<VirtualAudioStream> VirtualAudioStream::CreateStream(VirtualAudioDeviceImpl* owner,
                                                                 zx_device_t* devnode,
                                                                 bool is_input) {
  if (is_input) {
    return audio::SimpleAudioStream::Create<VirtualAudioStreamIn>(owner, devnode);
  } else {
    return audio::SimpleAudioStream::Create<VirtualAudioStreamOut>(owner, devnode);
  }
}

zx_status_t VirtualAudioStream::Init() {
  if (!strlcpy(device_name_, parent_->device_name_.c_str(), sizeof(device_name_))) {
    return ZX_ERR_INTERNAL;
  }

  if (!strlcpy(mfr_name_, parent_->mfr_name_.c_str(), sizeof(mfr_name_))) {
    return ZX_ERR_INTERNAL;
  }

  if (!strlcpy(prod_name_, parent_->prod_name_.c_str(), sizeof(prod_name_))) {
    return ZX_ERR_INTERNAL;
  }

  memcpy(unique_id_.data, parent_->unique_id_, sizeof(unique_id_.data));

  supported_formats_.reset();
  for (auto range : parent_->supported_formats_) {
    supported_formats_.push_back(range);
  }

  fifo_depth_ = parent_->fifo_depth_;
  external_delay_nsec_ = parent_->external_delay_nsec_;
  clock_domain_ = parent_->clock_domain_;

  max_buffer_frames_ = parent_->max_buffer_frames_;
  min_buffer_frames_ = parent_->min_buffer_frames_;
  modulo_buffer_frames_ = parent_->modulo_buffer_frames_;

  cur_gain_state_ = parent_->cur_gain_state_;

  audio_pd_notify_flags_t plug_flags = 0;
  if (parent_->hardwired_) {
    plug_flags |= AUDIO_PDNF_HARDWIRED;
  }
  if (parent_->async_plug_notify_) {
    plug_flags |= AUDIO_PDNF_CAN_NOTIFY;
  }
  if (parent_->plugged_) {
    plug_flags |= AUDIO_PDNF_PLUGGED;
  }
  SetInitialPlugState(plug_flags);

  using_alt_notifications_ = parent_->override_notification_frequency_;
  if (parent_->override_notification_frequency_) {
    alt_notifications_per_ring_ = parent_->notifications_per_ring_;
  }

  return ZX_OK;
}

void VirtualAudioStream::EnqueuePlugChange(bool plugged) {
  {
    fbl::AutoLock lock(&wakeup_queue_lock_);
    PlugType plug_change = (plugged ? PlugType::Plug : PlugType::Unplug);
    plug_queue_.push_back(plug_change);
  }

  plug_change_wakeup_.Post(dispatcher());
}

void VirtualAudioStream::EnqueueGainRequest(
    fuchsia::virtualaudio::Device::GetGainCallback gain_callback) {
  {
    fbl::AutoLock lock(&wakeup_queue_lock_);
    gain_queue_.push_back(std::move(gain_callback));
  }

  gain_request_wakeup_.Post(dispatcher());
}

void VirtualAudioStream::EnqueueFormatRequest(
    fuchsia::virtualaudio::Device::GetFormatCallback format_callback) {
  {
    fbl::AutoLock lock(&wakeup_queue_lock_);
    format_queue_.push_back(std::move(format_callback));
  }

  format_request_wakeup_.Post(dispatcher());
}

void VirtualAudioStream::EnqueueBufferRequest(
    fuchsia::virtualaudio::Device::GetBufferCallback buffer_callback) {
  {
    fbl::AutoLock lock(&wakeup_queue_lock_);
    buffer_queue_.push_back(std::move(buffer_callback));
  }

  buffer_request_wakeup_.Post(dispatcher());
}

void VirtualAudioStream::EnqueuePositionRequest(
    fuchsia::virtualaudio::Device::GetPositionCallback position_callback) {
  {
    fbl::AutoLock lock(&wakeup_queue_lock_);
    position_queue_.push_back(std::move(position_callback));
  }

  position_request_wakeup_.Post(dispatcher());
}

void VirtualAudioStream::EnqueueNotificationOverride(uint32_t notifications_per_ring) {
  {
    fbl::AutoLock lock(&wakeup_queue_lock_);
    notifs_queue_.push_back(notifications_per_ring);
  }

  set_notifications_wakeup_.Post(dispatcher());
}

void VirtualAudioStream::HandlePlugChanges() {
  audio::ScopedToken t(
      domain_token());  // This method handles posts to plug_change_wakeup_ sent to dispatcher().
  while (true) {
    PlugType plug_change;

    if (fbl::AutoLock lock(&wakeup_queue_lock_); !plug_queue_.empty()) {
      plug_change = plug_queue_.front();
      plug_queue_.pop_front();
    } else {
      break;
    }

    HandlePlugChange(plug_change);
  }
}

void VirtualAudioStream::HandlePlugChange(PlugType plug_change) {
  switch (plug_change) {
    case PlugType::Plug:
      SetPlugState(true);
      break;
    case PlugType::Unplug:
      SetPlugState(false);
      break;
      // Intentionally omitting default, so new enums surface a logic error.
  }
}

void VirtualAudioStream::HandleGainRequests() {
  audio::ScopedToken t(
      domain_token());  // This method handles posts to gain_request_wakeup_ sent to dispatcher().
  while (true) {
    bool current_mute, current_agc;
    float current_gain_db;
    fuchsia::virtualaudio::Device::GetGainCallback gain_callback;

    if (fbl::AutoLock lock(&wakeup_queue_lock_); !gain_queue_.empty()) {
      current_mute = cur_gain_state_.cur_mute;
      current_agc = cur_gain_state_.cur_agc;
      current_gain_db = cur_gain_state_.cur_gain;

      gain_callback = std::move(gain_queue_.front());
      gain_queue_.pop_front();
    } else {
      break;
    }

    parent_->PostToDispatcher(
        [gain_callback = std::move(gain_callback), current_mute, current_agc, current_gain_db]() {
          gain_callback(current_mute, current_agc, current_gain_db);
        });
  }
}

void VirtualAudioStream::HandleFormatRequests() {
  audio::ScopedToken t(
      domain_token());  // This method handles posts to format_request_wakeup_ sent to dispatcher().
  while (true) {
    uint32_t frames_per_second, sample_format, num_channels;
    zx_duration_t external_delay;
    fuchsia::virtualaudio::Device::GetFormatCallback format_callback;

    if (fbl::AutoLock lock(&wakeup_queue_lock_); !format_queue_.empty()) {
      frames_per_second = frame_rate_;
      sample_format = sample_format_;
      num_channels = num_channels_;
      external_delay = external_delay_nsec_;

      format_callback = std::move(format_queue_.front());
      format_queue_.pop_front();
    } else {
      break;
    }

    if (frames_per_second == 0) {
      zxlogf(WARNING, "Format is not set - should not be calling GetFormat");
      return;
    }

    parent_->PostToDispatcher([format_callback = std::move(format_callback), frames_per_second,
                               sample_format, num_channels, external_delay]() {
      format_callback(frames_per_second, sample_format, num_channels, external_delay);
    });
  }
}

void VirtualAudioStream::HandleBufferRequests() {
  audio::ScopedToken t(
      domain_token());  // This method handles posts to buffer_request_wakeup_ sent to dispatcher().
  while (true) {
    zx_status_t status;
    zx::vmo duplicate_ring_buffer_vmo;
    uint32_t num_ring_buffer_frames;
    uint32_t notifications_per_ring;
    fuchsia::virtualaudio::Device::GetBufferCallback buffer_callback;

    if (fbl::AutoLock lock(&wakeup_queue_lock_); !buffer_queue_.empty()) {
      if (!ring_buffer_vmo_.is_valid()) {
        zxlogf(WARNING, "Buffer is not set - should not be retrieving ring buffer");
        return;
      }
      status = ring_buffer_vmo_.duplicate(
          ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP,
          &duplicate_ring_buffer_vmo);
      num_ring_buffer_frames = num_ring_buffer_frames_;
      notifications_per_ring = notifications_per_ring_;

      buffer_callback = std::move(buffer_queue_.front());
      buffer_queue_.pop_front();
    } else {
      break;
    }

    if (status != ZX_OK) {
      zxlogf(ERROR, "%s failed to duplicate VMO handle - %d", __func__, status);
      return;
    }

    parent_->PostToDispatcher([buffer_callback = std::move(buffer_callback),
                               rb_vmo = std::move(duplicate_ring_buffer_vmo),
                               num_ring_buffer_frames, notifications_per_ring]() mutable {
      buffer_callback(std::move(rb_vmo), num_ring_buffer_frames, notifications_per_ring);
    });
  }
}

void VirtualAudioStream::HandlePositionRequests() {
  audio::ScopedToken t(domain_token());  // This method handles posts to position_request_wakeup_
                                         // sent to dispatcher().
  while (true) {
    zx::time start_time;
    uint32_t num_rb_frames, frame_size, frame_rate;
    fuchsia::virtualaudio::Device::GetPositionCallback position_callback;

    if (fbl::AutoLock lock(&wakeup_queue_lock_); !position_queue_.empty()) {
      start_time = start_time_;
      num_rb_frames = num_ring_buffer_frames_;
      frame_size = frame_size_;
      frame_rate = frame_rate_;

      position_callback = std::move(position_queue_.front());
      position_queue_.pop_front();
    } else {
      break;
    }

    if (start_time.get() == 0) {
      zxlogf(WARNING, "Stream is not started -- should not be calling GetPosition");
      return;
    }

    zx::time now = zx::clock::get_monotonic();
    zx::duration running_duration = now - start_time;
    uint64_t frames = (running_duration * frame_rate) / zx::sec(1);
    uint32_t ring_buffer_position = (frames % num_rb_frames) * frame_size;
    zx_time_t time_for_position = now.get();

    parent_->PostToDispatcher(
        [position_callback = std::move(position_callback), time_for_position,
         ring_buffer_position]() { position_callback(time_for_position, ring_buffer_position); });
  }
}

void VirtualAudioStream::HandleSetNotifications() {
  audio::ScopedToken t(domain_token());  // This method handles posts to set_notifications_wakeup_
                                         // sent to dispatcher().
  while (true) {
    if (fbl::AutoLock lock(&wakeup_queue_lock_); !notifs_queue_.empty()) {
      alt_notifications_per_ring_ = notifs_queue_.front();
      notifs_queue_.pop_front();

      // If alternate cadence is same as "official" cadence, use it instead.
      if (alt_notifications_per_ring_ == notifications_per_ring_) {
        using_alt_notifications_ = false;
        continue;
      }

      using_alt_notifications_ = true;
      if (alt_notifications_per_ring_ == 0) {
        alt_notification_period_ = zx::duration(0);
      } else {
        alt_notification_period_ = zx::duration((zx::sec(1) * num_ring_buffer_frames_) /
                                                (frame_rate_ * alt_notifications_per_ring_));
      }
    } else {
      break;
    }
  }
  if (using_alt_notifications_ && (alt_notification_period_.get() > 0)) {
    target_alt_notification_time_ = zx::clock::get_monotonic();
    alt_notify_timer_.PostForTime(dispatcher(), target_alt_notification_time_);
  } else {
    target_alt_notification_time_ = zx::time(0);
    alt_notify_timer_.Cancel();
  }
}

// Upon success, drivers should return a valid VMO with appropriate permissions (READ | MAP |
// TRANSFER for inputs, WRITE as well for outputs) as well as reporting the total number of usable
// frames in the ring.
//
// Format must already be set: a ring buffer channel (over which this command arrived) is provided
// as the return value from a successful SetFormat call.
zx_status_t VirtualAudioStream::GetBuffer(const audio::audio_proto::RingBufGetBufferReq& req,
                                          uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  if (req.notifications_per_ring > req.min_ring_buffer_frames) {
    zxlogf(ERROR, "req.notifications_per_ring too big");
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (req.min_ring_buffer_frames > max_buffer_frames_) {
    zxlogf(ERROR, "req.min_ring_buffer_frames too big");
    return ZX_ERR_OUT_OF_RANGE;
  }

  num_ring_buffer_frames_ = std::max(
      min_buffer_frames_,
      fbl::round_up<uint32_t, uint32_t>(req.min_ring_buffer_frames, modulo_buffer_frames_));
  uint32_t ring_buffer_size =
      fbl::round_up<size_t, size_t>(num_ring_buffer_frames_ * frame_size_, ZX_PAGE_SIZE);

  if (ring_buffer_mapper_.start() != nullptr) {
    ring_buffer_mapper_.Unmap();
  }

  zx_status_t status = ring_buffer_mapper_.CreateAndMap(
      ring_buffer_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &ring_buffer_vmo_,
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to create ring buffer vmo - %d", __func__, status);
    return status;
  }
  status = ring_buffer_vmo_.duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, out_buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to duplicate VMO handle for out param - %d", __func__, status);
    return status;
  }

  notifications_per_ring_ = req.notifications_per_ring;

  if (notifications_per_ring_ == 0) {
    notification_period_ = zx::duration(0);
  } else {
    notification_period_ = zx::duration((zx::sec(1) * num_ring_buffer_frames_) /
                                        (frame_rate_ * notifications_per_ring_));
  }
  if (using_alt_notifications_) {
    if (alt_notifications_per_ring_ == 0) {
      alt_notification_period_ = zx::duration(0);
    } else {
      alt_notification_period_ = zx::duration((zx::sec(1) * num_ring_buffer_frames_) /
                                              (frame_rate_ * alt_notifications_per_ring_));
    }
  }

  *out_num_rb_frames = num_ring_buffer_frames_;

  zx::vmo duplicate_vmo;
  status = ring_buffer_vmo_.duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &duplicate_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to duplicate VMO handle for notification - %d", __func__, status);
    return status;
  }
  parent_->NotifyBufferCreated(std::move(duplicate_vmo), num_ring_buffer_frames_,
                               notifications_per_ring_);

  return ZX_OK;
}

zx_status_t VirtualAudioStream::ChangeFormat(const audio::audio_proto::StreamSetFmtReq& req) {
  // frame_size_ is already set, automatically
  ZX_DEBUG_ASSERT(frame_size_);

  frame_rate_ = req.frames_per_second;
  ZX_DEBUG_ASSERT(frame_rate_);

  sample_format_ = req.sample_format;

  num_channels_ = req.channels;
  bytes_per_sec_ = frame_rate_ * frame_size_;

  // (Re)set external_delay_nsec_ and fifo_depth_ before leaving, if needed.

  parent_->NotifySetFormat(frame_rate_, sample_format_, num_channels_, external_delay_nsec_);

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

  parent_->NotifySetGain(cur_gain_state_.cur_mute, cur_gain_state_.cur_agc,
                         cur_gain_state_.cur_gain);

  return ZX_OK;
}

// Drivers *must* report the time at which the first frame will be clocked out on the
// CLOCK_MONOTONIC timeline, not including any external delay.
zx_status_t VirtualAudioStream::Start(uint64_t* out_start_time) {
  // Incorporate delay caused by fifo_depth_
  start_time_ =
      zx::clock::get_monotonic() + zx::duration((zx::sec(1) * fifo_depth_) / bytes_per_sec_);

  // Set the timer here (if notifications are enabled).
  if (notification_period_.get() > 0) {
    notify_timer_.PostForTime(dispatcher(), start_time_);
    target_notification_time_ = start_time_;
  }
  if (using_alt_notifications_ && alt_notification_period_.get() > 0) {
    alt_notify_timer_.PostForTime(dispatcher(), start_time_);
    target_alt_notification_time_ = start_time_;
  }

  parent_->NotifyStart(start_time_.get());
  *out_start_time = start_time_.get();

  return ZX_OK;
}

// Timer handler for sending out position notifications: to AudioCore, to VAD clients that do not
// override the notification frequency, and to VAD clients that set it to the same value that
// AudioCore has selected.
void VirtualAudioStream::ProcessRingNotification() {
  audio::ScopedToken t(
      domain_token());  // This method handles posts to notify_timer_ sent to dispatcher().
  ZX_DEBUG_ASSERT(notification_period_.get() > 0);

  auto monotonic_time = target_notification_time_.get();
  ZX_DEBUG_ASSERT(monotonic_time > 0);

  // TODO(fxbug.dev/13343): use a proper Timeline object here.
  auto running_duration = target_notification_time_ - start_time_;
  uint64_t frames = (running_duration * frame_rate_) / zx::sec(1);
  uint32_t ring_buffer_position = (frames % num_ring_buffer_frames_) * frame_size_;

  audio::audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;
  resp.monotonic_time = monotonic_time;
  resp.ring_buffer_pos = ring_buffer_position;

  NotifyPosition(resp);

  if (!using_alt_notifications_) {
    parent_->NotifyPosition(monotonic_time, ring_buffer_position);
  }

  target_notification_time_ += notification_period_;
  notify_timer_.PostForTime(dispatcher(), target_notification_time_);
}

// Handler for sending alternate position notifications: those going to VAD clients that specified a
// different notification frequency. These are not sent to AudioCore.
void VirtualAudioStream::ProcessAltRingNotification() {
  audio::ScopedToken t(
      domain_token());  // This method handles posts to alt_notify_timer_ sent to dispatcher().
  ZX_DEBUG_ASSERT(using_alt_notifications_);
  ZX_DEBUG_ASSERT(alt_notification_period_.get() > 0);

  auto monotonic_time = target_alt_notification_time_.get();
  ZX_DEBUG_ASSERT(monotonic_time > 0);

  // TODO(fxbug.dev/13343): use a proper Timeline object here.
  auto running_duration = target_alt_notification_time_ - start_time_;
  uint64_t frames = (running_duration * frame_rate_) / zx::sec(1);
  uint32_t ring_buffer_position = (frames % num_ring_buffer_frames_) * frame_size_;

  parent_->NotifyPosition(monotonic_time, ring_buffer_position);

  target_alt_notification_time_ += alt_notification_period_;
  alt_notify_timer_.PostForTime(dispatcher(), target_alt_notification_time_);
}

zx_status_t VirtualAudioStream::Stop() {
  auto stop_time = zx::clock::get_monotonic();

  notify_timer_.Cancel();
  alt_notify_timer_.Cancel();

  zx::duration running_duration = stop_time - start_time_;
  uint64_t frames = (running_duration * frame_rate_) / zx::sec(1);
  uint32_t ring_buf_position = (frames % num_ring_buffer_frames_) * frame_size_;
  parent_->NotifyStop(stop_time.get(), ring_buf_position);

  start_time_ = zx::time(0);
  target_notification_time_ = zx::time(0);
  target_alt_notification_time_ = zx::time(0);
  notification_period_ = zx::duration(0);
  alt_notification_period_ = zx::duration(0);

  return ZX_OK;
}

// Called by parent SimpleAudioStream::Shutdown, during DdkUnbind. If our parent is not shutting
// down, then someone else called our DdkUnbind (perhaps the DevHost is removing our driver), and we
// should let our parent know so that it does not later try to Unbind us. Knowing who started the
// unwinding allows this to proceed in an orderly way, in all cases.
void VirtualAudioStream::ShutdownHook() {
  if (!shutdown_by_parent_) {
    parent_->ClearStream();
  }
}

}  // namespace virtual_audio
