// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/audio/virtual_audio/virtual_audio_stream.h"

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <cmath>

#include "garnet/drivers/audio/virtual_audio/virtual_audio_device_impl.h"
#include "garnet/drivers/audio/virtual_audio/virtual_audio_stream_in.h"
#include "garnet/drivers/audio/virtual_audio/virtual_audio_stream_out.h"

namespace virtual_audio {

constexpr bool kTestPosition = false;

// static
fbl::RefPtr<VirtualAudioStream> VirtualAudioStream::CreateStream(
    VirtualAudioDeviceImpl* owner, zx_device_t* devnode, bool is_input) {
  if (is_input) {
    return ::audio::SimpleAudioStream::Create<VirtualAudioStreamIn>(owner,
                                                                    devnode);
  } else {
    return ::audio::SimpleAudioStream::Create<VirtualAudioStreamOut>(owner,
                                                                     devnode);
  }
}

VirtualAudioStream::~VirtualAudioStream() {
  ZX_DEBUG_ASSERT(domain_->deactivated());
}

zx_status_t VirtualAudioStream::Init() {
  if (!strlcpy(device_name_, parent_->device_name_, sizeof(device_name_))) {
    return ZX_ERR_INTERNAL;
  }

  if (!strlcpy(mfr_name_, parent_->mfr_name_, sizeof(mfr_name_))) {
    return ZX_ERR_INTERNAL;
  }

  if (!strlcpy(prod_name_, parent_->prod_name_, sizeof(prod_name_))) {
    return ZX_ERR_INTERNAL;
  }

  memcpy(unique_id_.data, parent_->unique_id_, sizeof(unique_id_.data));

  supported_formats_.reset();
  for (auto range : parent_->supported_formats_) {
    supported_formats_.push_back(range);
  }

  fifo_depth_ = parent_->fifo_depth_;
  external_delay_nsec_ = parent_->external_delay_nsec_;

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

  return ZX_OK;
}

zx_status_t VirtualAudioStream::InitPost() {
  plug_change_wakeup_ = dispatcher::WakeupEvent::Create();
  if (plug_change_wakeup_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  dispatcher::WakeupEvent::ProcessHandler plug_wake_handler(
      [this](dispatcher::WakeupEvent* event) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, domain_);
        HandlePlugChanges();
        return ZX_OK;
      });
  zx_status_t status =
      plug_change_wakeup_->Activate(domain_, std::move(plug_wake_handler));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Plug WakeupEvent activate failed (%d)\n", status);
    return status;
  }

  notify_timer_ = dispatcher::Timer::Create();
  if (notify_timer_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  dispatcher::Timer::ProcessHandler timer_handler(
      [this](dispatcher::Timer* timer) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, domain_);
        return ProcessRingNotification();
      });
  status = notify_timer_->Activate(domain_, std::move(timer_handler));
  if (status != ZX_OK) {
    zxlogf(ERROR, "PositionNotify Timer activate failed (%d)\n", status);
    return status;
  }

  return ZX_OK;
}

void VirtualAudioStream::HandlePlugChanges() {
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
    default:
      zxlogf(TRACE, "%s - Unknown message type\n", __PRETTY_FUNCTION__);
      break;
  }
}

// Upon success, drivers should return a valid VMO with appropriate
// permissions (READ | MAP | TRANSFER for inputs, WRITE as well for outputs)
// as well as reporting the total number of usable frames in the ring.
zx_status_t VirtualAudioStream::GetBuffer(
    const ::audio::audio_proto::RingBufGetBufferReq& req,
    uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  if (req.notifications_per_ring > req.min_ring_buffer_frames) {
    zxlogf(ERROR, "req.notifications_per_ring too big");
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (req.min_ring_buffer_frames > max_buffer_frames_) {
    zxlogf(ERROR, "req.min_ring_buffer_frames too big");
    return ZX_ERR_OUT_OF_RANGE;
  }

  num_ring_buffer_frames_ =
      std::max(min_buffer_frames_,
               fbl::round_up<uint32_t, uint32_t>(req.min_ring_buffer_frames,
                                                 modulo_buffer_frames_));
  uint32_t ring_buffer_size = fbl::round_up<size_t, size_t>(
      num_ring_buffer_frames_ * frame_size_, ZX_PAGE_SIZE);

  if (kTestPosition) {
    zxlogf(TRACE,
           "%s: cmd: %x, min_ring_buffer_frames: %u, notif_per_ring: %d. "
           "Result: rb_frames: %u, buffer_size: %u\n",
           __PRETTY_FUNCTION__, req.hdr.cmd, req.min_ring_buffer_frames,
           req.notifications_per_ring, num_ring_buffer_frames_,
           ring_buffer_size);
  }

  if (ring_buffer_mapper_.start() != nullptr) {
    ring_buffer_mapper_.Unmap();
  }

  zx_status_t status = ring_buffer_mapper_.CreateAndMap(
      ring_buffer_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, out_buffer,
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to create ring buffer vmo - %d\n", __func__,
           status);
    return status;
  }

  if (req.notifications_per_ring == 0) {
    us_per_notification_ = 0u;
  } else {
    us_per_notification_ = static_cast<uint32_t>(
        (ZX_SEC(1) * num_ring_buffer_frames_) /
        (ZX_USEC(1) * frame_rate_ * req.notifications_per_ring));
  }

  if (kTestPosition) {
    zxlogf(TRACE, "%s us_per_notification is %u\n", __PRETTY_FUNCTION__,
           us_per_notification_);
  }

  *out_num_rb_frames = num_ring_buffer_frames_;

  return ZX_OK;
}

zx_status_t VirtualAudioStream::ChangeFormat(
    const ::audio::audio_proto::StreamSetFmtReq& req) {
  // frame_size_ is already set, automatically
  ZX_DEBUG_ASSERT(frame_size_);

  frame_rate_ = req.frames_per_second;
  ZX_DEBUG_ASSERT(frame_rate_);

  num_channels_ = req.channels;
  bytes_per_sec_ = frame_rate_ * frame_size_;

  // (Re)set external_delay_nsec_ and fifo_depth_ before leaving, if needed.
  return ZX_OK;
}

void VirtualAudioStream::EnqueuePlugChange(bool plugged) {
  {
    fbl::AutoLock lock(&wakeup_queue_lock_);
    PlugType plug_change = (plugged ? PlugType::Plug : PlugType::Unplug);
    plug_queue_.push_back(plug_change);
  }

  plug_change_wakeup_->Signal();
}

zx_status_t VirtualAudioStream::SetGain(
    const ::audio::audio_proto::SetGainReq& req) {
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

  return ZX_OK;
}

// Drivers *must* report the time at which the first frame will be clocked out
// on the CLOCK_MONOTONIC timeline, not including any external delay.
zx_status_t VirtualAudioStream::Start(uint64_t* out_start_time) {
  start_time_ = zx::clock::get_monotonic() +
                // Incorporate delay caused by fifo_depth_
                zx::duration((ZX_SEC(1) * fifo_depth_) / bytes_per_sec_);

  if (kTestPosition) {
    zxlogf(TRACE, "%s at %ld, running at %d b/s\n", __PRETTY_FUNCTION__,
           start_time_.get(), bytes_per_sec_);
  }

  *out_start_time = start_time_.get();

  // Set the timer here (if notifications are enabled).
  if (us_per_notification_) {
    ProcessRingNotification();
  }

  return ZX_OK;
}

// Timer handler for sending out position notifications
zx_status_t VirtualAudioStream::ProcessRingNotification() {
  ZX_DEBUG_ASSERT(us_per_notification_ > 0);

  zx::time now = zx::clock::get_monotonic();
  notify_timer_->Arm(now.get() + ZX_USEC(us_per_notification_));

  ::audio::audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  zx::duration duration_ns = now - start_time_;
  // TODO(mpuryear): use a proper Timeline object here. Reference MTWN-57.
  uint64_t frames = (duration_ns.get() * frame_rate_) / ZX_SEC(1);
  resp.ring_buffer_pos = (frames % num_ring_buffer_frames_) * frame_size_;

  if (kTestPosition) {
    zxlogf(TRACE, "%s at %08x, %ld\n", __PRETTY_FUNCTION__,
           resp.ring_buffer_pos, now.get());
  }

  return NotifyPosition(resp);
}

zx_status_t VirtualAudioStream::Stop() {
  if (kTestPosition) {
    zxlogf(TRACE, "%s at %ld\n", __PRETTY_FUNCTION__,
           zx_clock_get(ZX_CLOCK_MONOTONIC));
  }

  notify_timer_->Cancel();
  start_time_ = zx::time(0);

  return ZX_OK;
}

// Called by parent SimpleAudioStream::Shutdown, during DdkUnbind.
// If our parent is not shutting down, then someone else called our DdkUnbind
// (perhaps the DevHost is removing our driver), and we should let our parent
// know so that it does not later try to Unbind us. Knowing who started the
// unwinding allows this to proceed in an orderly way, in all cases.
void VirtualAudioStream::ShutdownHook() {
  if (!shutdown_by_parent_) {
    parent_->ClearStream();
  }
}

}  // namespace virtual_audio
