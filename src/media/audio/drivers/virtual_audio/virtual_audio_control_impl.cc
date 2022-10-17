// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/drivers/virtual_audio/virtual_audio_control_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/zx/status.h>

#include <ddktl/fidl.h>

#include "src/media/audio/drivers/virtual_audio/virtual_audio_bind.h"
#include "src/media/audio/drivers/virtual_audio/virtual_audio_device_impl.h"

namespace virtual_audio {

// static
zx_status_t VirtualAudioControlImpl::DdkBind(void* ctx, zx_device_t* parent_bus) {
  std::unique_ptr<VirtualAudioControlImpl> control(new VirtualAudioControlImpl);

  // Define entry-point operations for this control device.
  static zx_protocol_device_t device_ops = {
      .version = DEVICE_OPS_VERSION,
      .unbind = &DdkUnbind,
      .release = &DdkRelease,
      .message = &DdkMessage,
  };

  // Define other metadata, incl. "control" as our entry-point context.
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "virtual_audio";
  args.ctx = control.get();
  args.ops = &device_ops;
  args.flags = DEVICE_ADD_NON_BINDABLE;

  // Add the virtual_audio device node under the given parent.
  zx_status_t status = device_add(parent_bus, &args, &control->dev_node_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "*** %s: could not add device '%s': %d", __func__, args.name, status);
    return status;
  }

  zxlogf(INFO, "*** %s: added device '%s': %d", __func__, args.name, status);

  // Use the dispatcher supplied by the driver runtime.
  control->dispatcher_ = fdf::Dispatcher::GetCurrent()->async_dispatcher();

  // On successful Add, Devmgr takes ownership (relinquished on DdkRelease), so transfer our
  // ownership to a local var, and let it go out of scope.
  [[maybe_unused]] auto temp_ref = control.release();

  return ZX_OK;
}

// static
void VirtualAudioControlImpl::DdkUnbind(void* ctx) {
  ZX_ASSERT(ctx);

  auto self = static_cast<VirtualAudioControlImpl*>(ctx);
  if (self->devices_.empty()) {
    device_unbind_reply(self->dev_node_);
    return;
  }

  // Close any remaining device bindings, freeing those drivers.
  auto remaining = std::make_shared<size_t>(self->devices_.size());

  for (auto& d : self->devices_) {
    d->ShutdownAsync([remaining, self]() {
      ZX_ASSERT(*remaining > 0);
      // After all devices are gone we can remove the control device itself.
      if (--(*remaining) == 0) {
        device_unbind_reply(self->dev_node_);
      }
    });
  }
  self->devices_.clear();
}

// static
void VirtualAudioControlImpl::DdkRelease(void* ctx) {
  ZX_ASSERT(ctx);

  // Always called after DdkUnbind.
  // By now, all our lists should be empty and we can destroy the ctx.
  std::unique_ptr<VirtualAudioControlImpl> control_ptr(static_cast<VirtualAudioControlImpl*>(ctx));
  ZX_ASSERT(control_ptr->devices_.empty());
}

// static
zx_status_t VirtualAudioControlImpl::DdkMessage(void* ctx, fidl_incoming_msg_t* msg,
                                                fidl_txn_t* txn) {
  VirtualAudioControlImpl* self = static_cast<VirtualAudioControlImpl*>(ctx);
  DdkTransaction transaction(txn);
  fidl::WireDispatch<fuchsia_virtualaudio::Control>(
      self, fidl::IncomingHeaderAndMessage::FromEncodedCMessage(msg), &transaction);
  return transaction.Status();
}

namespace {
VirtualAudioDeviceImpl::Config DefaultConfig(bool is_input) {
  VirtualAudioDeviceImpl::Config config;
  config.is_input = is_input;

  config.device_name = "Virtual Audio Device";
  // Sibling devices cannot have duplicate names, so differentiate them based on direction.
  config.device_name += (is_input ? " (input)" : " (output)");

  config.manufacturer_name = "Fuchsia Virtual Audio Group";
  config.product_name = "Virgil v1, a Virtual Volume Vessel";
  config.unique_id = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};

  // Default FIFO is 250 usec, at 48k stereo 16
  config.fifo_depth_bytes = 48;
  config.external_delay = zx::nsec(0);

  // Default is 48kHz stereo 16bit.
  config.supported_formats.push_back({
      .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
      .min_frames_per_second = 48000,
      .max_frames_per_second = 48000,
      .min_channels = 2,
      .max_channels = 2,
      .flags = ASF_RANGE_FLAG_FPS_48000_FAMILY,
  });

  // Default is CLOCK_MONOTONIC.
  config.clock.domain = 0;
  config.clock.initial_rate_adjustment_ppm = 0;

  // Default ring buffer size is at least 250msec (assuming default rate 48k).
  config.ring_buffer.min_frames = 12000;
  config.ring_buffer.max_frames = 1 << 19;  // (10+ sec, at default 48k!)
  config.ring_buffer.modulo_frames = 1;

  // By default, support a wide gain range with good precision.
  config.gain = fuchsia_virtualaudio::wire::GainProperties{
      .min_gain_db = -160.f,
      .max_gain_db = 24.f,
      .gain_step_db = 0.25f,
      .current_gain_db = -0.75f,
      .can_mute = true,
      .current_mute = false,
      .can_agc = false,
      .current_agc = false,
  };

  // By default, device is hot-pluggable
  config.plug.plug_change_time = zx::clock::get_monotonic().get();
  config.plug.plugged = true;
  config.plug.hardwired = false;
  config.plug.can_notify = true;

  return config;
}

zx::result<VirtualAudioDeviceImpl::Config> ConfigFromFIDL(
    const fuchsia_virtualaudio::wire::Configuration& fidl, bool is_input) {
  auto config = DefaultConfig(is_input);

  if (fidl.has_device_name()) {
    config.device_name = std::string(fidl.device_name().get());
  }
  if (fidl.has_manufacturer_name()) {
    config.manufacturer_name = std::string(fidl.manufacturer_name().get());
  }
  if (fidl.has_product_name()) {
    config.product_name = std::string(fidl.product_name().get());
  }
  if (fidl.has_unique_id()) {
    memmove(config.unique_id.data(), fidl.unique_id().data(), sizeof(config.unique_id));
  }
  if (fidl.has_fifo_depth_bytes()) {
    config.fifo_depth_bytes = fidl.fifo_depth_bytes();
  }
  if (fidl.has_external_delay()) {
    config.external_delay = zx::nsec(fidl.external_delay());
  }
  if (fidl.has_supported_formats()) {
    config.supported_formats.clear();
    for (auto& fmt : fidl.supported_formats()) {
      if (fmt.min_frame_rate > fmt.max_frame_rate) {
        zxlogf(ERROR, "Invalid FormatRange: min_frame_rate=%u > max_frame_rate=%u",
               fmt.min_frame_rate, fmt.max_frame_rate);
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      if (fmt.min_channels > fmt.max_channels) {
        zxlogf(ERROR, "Invalid FormatRange: min_channels=%u > max_channels=%u", fmt.min_channels,
               fmt.max_channels);
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      config.supported_formats.push_back({
          .sample_formats = fmt.sample_format_flags,
          .min_frames_per_second = fmt.min_frame_rate,
          .max_frames_per_second = fmt.max_frame_rate,
          .min_channels = fmt.min_channels,
          .max_channels = fmt.max_channels,
          .flags = fmt.rate_family_flags,
      });
    }
  }
  if (fidl.has_clock_properties()) {
    auto& fidl_clock = fidl.clock_properties();
    if (fidl_clock.initial_rate_adjustment_ppm != 0 && fidl_clock.domain == 0) {
      zxlogf(ERROR, "Invalid ClockProperties: domain=%u, initial_rate_adjustment_ppm=%u",
             fidl_clock.domain, fidl_clock.initial_rate_adjustment_ppm);
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    config.clock = fidl_clock;
  }
  if (fidl.has_ring_buffer_constraints()) {
    auto& fidl_rb = fidl.ring_buffer_constraints();
    if (fidl_rb.modulo_frames == 0 || fidl_rb.min_frames > fidl_rb.max_frames ||
        fidl_rb.min_frames % fidl_rb.modulo_frames != 0 ||
        fidl_rb.max_frames % fidl_rb.modulo_frames != 0) {
      zxlogf(ERROR, "Invalid RingBufferConstraints: min_frames=%u, max_frames=%u, modulo_frames=%u",
             fidl_rb.min_frames, fidl_rb.max_frames, fidl_rb.modulo_frames);
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    config.ring_buffer = fidl_rb;
  }
  if (fidl.has_gain_properties()) {
    config.gain = fidl.gain_properties();
  }
  if (fidl.has_plug_properties()) {
    config.plug = fidl.plug_properties();
  }
  if (fidl.has_initial_notifications_per_ring()) {
    config.initial_notifications_per_ring = fidl.initial_notifications_per_ring();
  }

  return zx::ok(config);
}
}  // namespace

void VirtualAudioControlImpl::AddInput(AddInputRequestView request,
                                       AddInputCompleter::Sync& completer) {
  auto cfg = ConfigFromFIDL(request->config, true);
  auto result = VirtualAudioDeviceImpl::Create(cfg.value(), std::move(request->server), dev_node_,
                                               dispatcher_);
  if (!result.is_ok()) {
    zxlogf(ERROR, "Input device creation failed with status %d",
           fidl::ToUnderlying(result.error_value()));
    completer.ReplyError(result.error_value());
    return;
  }
  auto device = result.value();
  devices_.insert(device);
  completer.ReplySuccess();
}

void VirtualAudioControlImpl::AddOutput(AddOutputRequestView request,
                                        AddOutputCompleter::Sync& completer) {
  auto cfg = ConfigFromFIDL(request->config, false);
  auto result = VirtualAudioDeviceImpl::Create(cfg.value(), std::move(request->server), dev_node_,
                                               dispatcher_);
  if (!result.is_ok()) {
    zxlogf(ERROR, "Output device creation failed with status %d",
           fidl::ToUnderlying(result.error_value()));
    completer.ReplyError(result.error_value());
    return;
  }
  auto device = result.value();
  devices_.insert(device);
  completer.ReplySuccess();
}

void VirtualAudioControlImpl::GetNumDevices(GetNumDevicesCompleter::Sync& completer) {
  uint32_t num_inputs = 0;
  uint32_t num_outputs = 0;
  for (auto& d : devices_) {
    if (d->is_input()) {
      num_inputs++;
    } else {
      num_outputs++;
    }
  }
  completer.Reply(num_inputs, num_outputs);
}

void VirtualAudioControlImpl::RemoveAll(RemoveAllCompleter::Sync& completer) {
  if (devices_.empty()) {
    completer.Reply();
    return;
  }

  // This callback waits until all devices have shut down. We wrap the async completer in a
  // shared_ptr so the callback can be copied into each ShutdownAsync call.
  struct ShutdownState {
    explicit ShutdownState(RemoveAllCompleter::Sync& sync) : completer(sync.ToAsync()) {}
    RemoveAllCompleter::Async completer;
    size_t remaining;
  };
  auto state = std::make_shared<ShutdownState>(completer);
  state->remaining = devices_.size();

  for (auto& d : devices_) {
    d->ShutdownAsync([state]() {
      ZX_ASSERT(state->remaining > 0);
      // After all devices are gone, notify the completer.
      if ((--state->remaining) == 0) {
        state->completer.Reply();
      }
    });
  }
  devices_.clear();
}

}  // namespace virtual_audio
