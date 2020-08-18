// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/drivers/virtual_audio/virtual_audio_device_impl.h"

#include <lib/zx/clock.h>

#include <memory>

#include <ddk/debug.h>

#include "src/media/audio/drivers/virtual_audio/virtual_audio_stream.h"

namespace virtual_audio {

class VirtualAudioStreamIn;
class VirtualAudioStreamOut;

// static
std::unique_ptr<VirtualAudioDeviceImpl> VirtualAudioDeviceImpl::Create(
    VirtualAudioControlImpl* owner, bool is_input) {
  return std::unique_ptr<VirtualAudioDeviceImpl>(new VirtualAudioDeviceImpl(owner, is_input));
}

// Don't initialize here or in ctor; do it all in Init() so ResetConfiguration has same effect.
VirtualAudioDeviceImpl::VirtualAudioDeviceImpl(VirtualAudioControlImpl* owner, bool is_input)
    : owner_(owner), is_input_(is_input) {
  ZX_DEBUG_ASSERT(owner_);

  Init();
}

// If we have not already destroyed our child stream, do so now.
VirtualAudioDeviceImpl::~VirtualAudioDeviceImpl() { RemoveStream(); }

// Initialize (or reset) a device's static configuration.
void VirtualAudioDeviceImpl::Init() {
  device_name_ = kDefaultDeviceName;
  mfr_name_ = kDefaultManufacturerName;
  prod_name_ = kDefaultProductName;
  memcpy(unique_id_, kDefaultUniqueId, sizeof(unique_id_));
  clock_domain_ = kDefaultClockDomain;

  // By default, we support one basic format range (stereo 16-bit 48kHz)
  supported_formats_.clear();
  supported_formats_.push_back(kDefaultFormatRange);

  fifo_depth_ = kDefaultFifoDepthBytes;
  external_delay_nsec_ = kDefaultExternalDelayNsec;

  min_buffer_frames_ = kDefaultMinBufferFrames;
  max_buffer_frames_ = kDefaultMaxBufferFrames;
  modulo_buffer_frames_ = kDefaultModuloBufferFrames;

  cur_gain_state_ = kDefaultGainState;

  hardwired_ = kDefaultHardwired;
  async_plug_notify_ = kDefaultPlugCanNotify;
  plugged_ = kDefaultPlugged;

  // The time this Configuration was created / reset.
  plug_time_ = zx::clock::get_monotonic().get();

  override_notification_frequency_ = false;
}

// Receive and cache a virtualaudio::Input binding, forwarded from the virtual_audio_service.
// ControlImpl has already associated us with the binding: when it closes we will auto-destruct.
void VirtualAudioDeviceImpl::SetBinding(
    fidl::Binding<fuchsia::virtualaudio::Input,
                  std::unique_ptr<virtual_audio::VirtualAudioDeviceImpl>>* binding) {
  ZX_ASSERT(is_input_);
  ZX_DEBUG_ASSERT(output_binding_ == nullptr);

  input_binding_ = binding;
  ZX_DEBUG_ASSERT(input_binding_->is_bound());
}

// Receive and cache a virtualaudio::Output binding, forwarded from the virtual_audio_service.
// ControlImpl has already associated us with the binding: when it closes we will auto-destruct.
void VirtualAudioDeviceImpl::SetBinding(
    fidl::Binding<fuchsia::virtualaudio::Output,
                  std::unique_ptr<virtual_audio::VirtualAudioDeviceImpl>>* binding) {
  ZX_ASSERT(!is_input_);
  ZX_DEBUG_ASSERT(input_binding_ == nullptr);

  output_binding_ = binding;
  ZX_DEBUG_ASSERT(output_binding_->is_bound());
}

// In response to an Input::Add or Output::Add call, create the stream (activate the device).
bool VirtualAudioDeviceImpl::CreateStream(zx_device_t* devnode) {
  stream_ = VirtualAudioStream::CreateStream(this, devnode, is_input_);
  return (stream_ != nullptr);
}

// Remove our child stream by calling its DdkUnbind. This may already have occurred; check for null.
//
// TODO(mpuryear): This may not safely unwind in all cases: it makes some threading assumptions that
// cannot necessarily be enforced. But until ZX-3461 is addressed, the current VAD code appears to
// be safe -- all RemoveStream callers are on the devhost primary thread:
//   ~VirtualAudioDeviceImpl from DevHost removing parent,
//   ~VirtualAudioDeviceImpl from Input|Output FIDL channel disconnecting
//   fuchsia.virtualaudio.Control.Disable
//   fuchsia.virtualaudio.Input|Output.Remove
void VirtualAudioDeviceImpl::RemoveStream() {
  if (stream_ != nullptr) {
    // Clear any queued dispatcher tasks, and no longer accept new tasks.
    task_queue_->StopAndClear();

    // This bool tells the stream that Unbind originates from us (parent), so don't call us back.
    stream_->shutdown_by_parent_ = true;

    // Shutdown won't return until any in-flight calls to PostToDispatcher are quiesced.
    // Shutdown deactivates its execution domain, and all PostToDispatcher calls are
    // made from this execution domain). This guarantee is critical, because Add changes what
    // task_queue_ points to. If calls to PostToDispatcher WERE still outstanding, then it would be
    // possible that Add could be called before task_queue_->Enqueue runs, causing us to erroneously
    // run a task associated with the older stream, in the context of the new stream.
    stream_->Shutdown();
    // Request the device be unbound.
    stream_->DdkAsyncRemove();

    // Now that the stream has done its shutdown, we release our reference.
    stream_ = nullptr;
  }
  // bindings are reset in the VirtualAudioDeviceImpl dtor
}

// In most cases, stream DdkUnbind is called by us (the parent DeviceImpl), above in RemoveStream.
// In the other cases, this RemoveStream is eventually called, but we should not RE-call DdkUnbind
// at that point. To cover those other cases, this ClearStream method allows the child stream to
// signal to its parent that it has gone away. Conversely, the flag shutdown_by_parent_ provides the
// device->stream signal in the other cases (such as when RemoveStream is called during the device
// dtor), to let the stream know that it need not and should not call us.
void VirtualAudioDeviceImpl::ClearStream() {
  // Clear any queued dispatcher tasks, and no longer accept new tasks.
  task_queue_->StopAndClear();

  // Forget about this stream, but don't do more right now -- our RemoveStream will be called later.
  stream_ = nullptr;
}

// Use closure_queue to post a dispatcher task -- cancellable if stream or device is destroyed.
void VirtualAudioDeviceImpl::PostToDispatcher(fit::closure task_to_post) {
  // We know task_queue_ isn't changing which ClosureQueue it's holding as this line is running,
  // because RemoveStream always runs on the same thread as Add (the main devhost thread, the FIDL
  // dispatch thread, the ClosureQueue's destination thread), and RemoveStream quiesces all calls to
  // PostToDispatcher before it returns (and thus before Add changes what task_queue_ points to).
  task_queue_->Enqueue(std::move(task_to_post));
}

//
// virtualaudio::Configuration implementation
//
void VirtualAudioDeviceImpl::SetDeviceName(std::string device_name) { device_name_ = device_name; };

void VirtualAudioDeviceImpl::SetManufacturer(std::string manufacturer_name) {
  mfr_name_ = manufacturer_name;
};

void VirtualAudioDeviceImpl::SetProduct(std::string product_name) { prod_name_ = product_name; };

void VirtualAudioDeviceImpl::SetUniqueId(std::array<uint8_t, 16> unique_id) {
  memcpy(unique_id_, unique_id.data(), sizeof(unique_id_));
};

// After creation or reset, one default format range is always available.
void VirtualAudioDeviceImpl::AddFormatRange(uint32_t format_flags, uint32_t min_rate,
                                            uint32_t max_rate, uint8_t min_chans, uint8_t max_chans,
                                            uint16_t rate_family_flags) {
  audio_stream_format_range_t range = {.sample_formats = format_flags,
                                       .min_frames_per_second = min_rate,
                                       .max_frames_per_second = max_rate,
                                       .min_channels = min_chans,
                                       .max_channels = max_chans,
                                       .flags = rate_family_flags};

  supported_formats_.push_back(range);
};

void VirtualAudioDeviceImpl::ClearFormatRanges() { supported_formats_.clear(); }

void VirtualAudioDeviceImpl::SetClockDomain(int32_t clock_domain) { clock_domain_ = clock_domain; };

void VirtualAudioDeviceImpl::SetFifoDepth(uint32_t fifo_depth_bytes) {
  fifo_depth_ = fifo_depth_bytes;
}

void VirtualAudioDeviceImpl::SetExternalDelay(zx_duration_t external_delay) {
  external_delay_nsec_ = external_delay;
};

void VirtualAudioDeviceImpl::SetRingBufferRestrictions(uint32_t min_frames, uint32_t max_frames,
                                                       uint32_t modulo_frames) {
  ZX_DEBUG_ASSERT(min_frames <= max_frames);
  ZX_DEBUG_ASSERT(min_frames % modulo_frames == 0);
  ZX_DEBUG_ASSERT(max_frames % modulo_frames == 0);

  min_buffer_frames_ = min_frames;
  max_buffer_frames_ = max_frames;
  modulo_buffer_frames_ = modulo_frames;
};

void VirtualAudioDeviceImpl::SetGainProperties(float min_gain_db, float max_gain_db,
                                               float gain_step_db, float current_gain_db,
                                               bool can_mute, bool current_mute, bool can_agc,
                                               bool current_agc) {
  cur_gain_state_ = {.cur_mute = current_mute,
                     .cur_agc = current_agc,
                     .cur_gain = current_gain_db,

                     .can_mute = can_mute,
                     .can_agc = can_agc,

                     .min_gain = min_gain_db,
                     .max_gain = max_gain_db,
                     .gain_step = gain_step_db};
};

void VirtualAudioDeviceImpl::SetPlugProperties(zx_time_t plug_change_time, bool plugged,
                                               bool hardwired, bool can_notify) {
  plug_time_ = plug_change_time;

  plugged_ = plugged;
  hardwired_ = hardwired;
  async_plug_notify_ = can_notify;
};

void VirtualAudioDeviceImpl::ResetConfiguration() { Init(); };

//
// virtualaudio::Device implementation
//
// Create a virtual audio device using the currently-specified configuration.
void VirtualAudioDeviceImpl::Add() {
  if (!owner_->enabled()) {
    zxlogf(WARNING, "%s: Disabled, cannot add stream", __func__);
    return;
  }

  if (stream_ != nullptr) {
    zxlogf(WARNING, "%s: %p already has stream %p", __func__, this, stream_.get());
    return;
  }

  // This ClosureQueue is created when stream is created), deactivated (via StopAndClear) when
  // stream is removed, and synchronized by being on devhost dispatcher thread (our FIDL thread).
  // DeviceImpl and Stream both call PostToDispatcher, and all are quiesced before we get here.
  // DeviceImpl makes those calls on the FIDL thread (task_queue_'s destination thread); all are
  // enqueued and subsequently discarded (via StopAndClear) upon RemoveStream. Stream makes those
  // calls in its execution domain, which is synchronously deactivated during its DdkUnbind.
  task_queue_.emplace(owner_->dispatcher(), thrd_current());
  if (!CreateStream(owner_->dev_node())) {
    zxlogf(ERROR, "CreateStream failed");
    task_queue_->StopAndClear();
    return;
  }
}

// Remove the associated virtual audio device.
void VirtualAudioDeviceImpl::Remove() {
  if (!owner_->enabled()) {
    zxlogf(WARNING, "%s: Disabled, no streams for removal", __func__);
    ZX_DEBUG_ASSERT(stream_ == nullptr);
    return;
  }

  if (stream_ == nullptr) {
    zxlogf(WARNING, "%s: %p has no stream to remove", __func__, this);
    return;
  }

  // If stream_ exists, null our copy and call SimpleAudioStream::DdkUnbind (which eventually calls
  // ShutdownHook and re-nulls). We need this because stream terminations come either from "device"
  // (direct DdkUnbind call), or from "parent" (Control::Disable, Device::Remove, ~DeviceImpl).
  RemoveStream();
}

void VirtualAudioDeviceImpl::GetFormat(
    fuchsia::virtualaudio::Device::GetFormatCallback format_callback) {
  if (stream_ == nullptr) {
    zxlogf(WARNING, "%s: %p has no stream for this request", __func__, this);
    return;
  }

  stream_->EnqueueFormatRequest(std::move(format_callback));
}

// Deliver SetFormat notification on binding's thread, if binding is valid.
void VirtualAudioDeviceImpl::NotifySetFormat(uint32_t frames_per_second, uint32_t sample_format,
                                             uint32_t num_channels, zx_duration_t external_delay) {
  PostToDispatcher([this, frames_per_second, sample_format, num_channels, external_delay]() {
    if (input_binding_ && input_binding_->is_bound()) {
      input_binding_->events().OnSetFormat(frames_per_second, sample_format, num_channels,
                                           external_delay);
    } else if (output_binding_ && output_binding_->is_bound()) {
      output_binding_->events().OnSetFormat(frames_per_second, sample_format, num_channels,
                                            external_delay);
    }
  });
}

void VirtualAudioDeviceImpl::GetGain(fuchsia::virtualaudio::Device::GetGainCallback gain_callback) {
  if (stream_ == nullptr) {
    zxlogf(WARNING, "%s: %p has no stream for this request", __func__, this);
    return;
  }

  stream_->EnqueueGainRequest(std::move(gain_callback));
}

// Deliver SetGain notification on binding's thread, if binding is valid.
void VirtualAudioDeviceImpl::NotifySetGain(bool current_mute, bool current_agc,
                                           float current_gain_db) {
  PostToDispatcher([this, current_mute, current_agc, current_gain_db]() {
    if (input_binding_ && input_binding_->is_bound()) {
      input_binding_->events().OnSetGain(current_mute, current_agc, current_gain_db);
    } else if (output_binding_ && output_binding_->is_bound()) {
      output_binding_->events().OnSetGain(current_mute, current_agc, current_gain_db);
    }
  });
}

void VirtualAudioDeviceImpl::GetBuffer(
    fuchsia::virtualaudio::Device::GetBufferCallback buffer_callback) {
  if (stream_ == nullptr) {
    zxlogf(WARNING, "%s: %p has no stream for this request", __func__, this);
    return;
  }

  stream_->EnqueueBufferRequest(std::move(buffer_callback));
}

// Deliver SetBuffer notification on binding's thread, if binding is valid.
void VirtualAudioDeviceImpl::NotifyBufferCreated(zx::vmo ring_buffer_vmo,
                                                 uint32_t num_ring_buffer_frames,
                                                 uint32_t notifications_per_ring) {
  PostToDispatcher([this, ring_buffer_vmo = std::move(ring_buffer_vmo), num_ring_buffer_frames,
                    notifications_per_ring]() mutable {
    if (input_binding_ && input_binding_->is_bound()) {
      input_binding_->events().OnBufferCreated(std::move(ring_buffer_vmo), num_ring_buffer_frames,
                                               notifications_per_ring);
    } else if (output_binding_ && output_binding_->is_bound()) {
      output_binding_->events().OnBufferCreated(std::move(ring_buffer_vmo), num_ring_buffer_frames,
                                                notifications_per_ring);
    }
  });
}

// Override AudioCore's systemwide position notification cadence, in favor of a per-stream
// notification cadence. Update the static config, and if active, tell device to dynamically change.
void VirtualAudioDeviceImpl::SetNotificationFrequency(uint32_t notifications_per_ring) {
  // This is a DeviceImpl property (stream has a property with same name)
  override_notification_frequency_ = true;
  notifications_per_ring_ = notifications_per_ring;

  if (stream_ != nullptr) {
    stream_->EnqueueNotificationOverride(notifications_per_ring);
  }
}

// Deliver Start notification on binding's thread, if binding is valid.
void VirtualAudioDeviceImpl::NotifyStart(zx_time_t start_time) {
  PostToDispatcher([this, start_time]() {
    if (is_input_) {
      ZX_ASSERT(input_binding_ && input_binding_->is_bound());
      input_binding_->events().OnStart(start_time);
    } else {
      ZX_ASSERT(output_binding_ && output_binding_->is_bound());
      output_binding_->events().OnStart(start_time);
    }
  });
}

// Deliver Stop notification on binding's thread, if binding is valid.
void VirtualAudioDeviceImpl::NotifyStop(zx_time_t stop_time, uint32_t ring_buffer_position) {
  PostToDispatcher([this, stop_time, ring_buffer_position]() {
    if (is_input_) {
      ZX_ASSERT(input_binding_ && input_binding_->is_bound());
      input_binding_->events().OnStop(stop_time, ring_buffer_position);
    } else {
      ZX_ASSERT(output_binding_ && output_binding_->is_bound());
      output_binding_->events().OnStop(stop_time, ring_buffer_position);
    }
  });
}

void VirtualAudioDeviceImpl::GetPosition(
    fuchsia::virtualaudio::Device::GetPositionCallback position_callback) {
  if (stream_ == nullptr) {
    zxlogf(WARNING, "%s: %p has no stream for this request", __func__, this);
    return;
  }

  stream_->EnqueuePositionRequest(std::move(position_callback));
}

// Deliver Position notification on binding's thread, if binding is valid.
void VirtualAudioDeviceImpl::NotifyPosition(zx_time_t monotonic_time,
                                            uint32_t ring_buffer_position) {
  PostToDispatcher([this, monotonic_time, ring_buffer_position]() {
    if (is_input_) {
      ZX_ASSERT(input_binding_ && input_binding_->is_bound());
      input_binding_->events().OnPositionNotify(monotonic_time, ring_buffer_position);
    } else {
      ZX_ASSERT(output_binding_ && output_binding_->is_bound());
      output_binding_->events().OnPositionNotify(monotonic_time, ring_buffer_position);
    }
  });
}

// Change the plug state on-the-fly for this active virtual audio device.
void VirtualAudioDeviceImpl::ChangePlugState(zx_time_t plug_change_time, bool plugged) {
  if (!owner_->enabled()) {
    zxlogf(WARNING, "%s: Disabled, cannot change plug state", __func__);
    return;
  }

  // Update static config, and tell (if present) stream to dynamically change.
  plug_time_ = plug_change_time;
  plugged_ = plugged;

  if (stream_ == nullptr) {
    zxlogf(WARNING, "%s: %p has no stream; cannot change dynamic plug state", __func__, this);
    return;
  }

  stream_->EnqueuePlugChange(plugged);
}

}  // namespace virtual_audio
