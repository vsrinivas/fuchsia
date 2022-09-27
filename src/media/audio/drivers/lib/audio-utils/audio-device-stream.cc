// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/audio.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <limits>

#include <audio-proto-utils/format-utils.h>
#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

namespace audio {
namespace utils {

AudioDeviceStream::AudioDeviceStream(StreamDirection direction, uint32_t dev_id)
    : direction_(direction) {
  snprintf(name_, sizeof(name_), "/dev/class/audio-%s/%03u",
           direction == StreamDirection::kInput ? "input" : "output", dev_id);
}

AudioDeviceStream::AudioDeviceStream(StreamDirection direction, const char* dev_path)
    : direction_(direction) {
  strncpy(name_, dev_path, sizeof(name_) - 1);
  name_[sizeof(name_) - 1] = 0;
}

AudioDeviceStream::~AudioDeviceStream() { Close(); }

zx_status_t AudioDeviceStream::Open() {
  if (stream_ch_.is_valid())
    return ZX_ERR_BAD_STATE;

  auto client_end = component::Connect<audio_fidl::StreamConfigConnector>(name());
  if (client_end.is_error()) {
    printf("component::Connect failed with error %s\n", client_end.status_string());
    return client_end.status_value();
  }
  fidl::WireSyncClient client{std::move(client_end.value())};

  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfig>();
  ZX_ASSERT(endpoints.is_ok());
  auto [stream_channel_local, stream_channel_remote] = *std::move(endpoints);
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)client->Connect(std::move(stream_channel_remote));

  stream_ch_ = std::move(stream_channel_local);
  return ZX_OK;
}

zx_status_t AudioDeviceStream::GetSupportedFormats(const SupportedFormatsCallback& cb) const {
  auto formats = fidl::WireCall(stream_ch_)->GetSupportedFormats();
  for (auto& i : formats.value().supported_formats) {
    cb(i);
  }
  return ZX_OK;
}

zx_status_t AudioDeviceStream::WatchPlugState(
    audio_stream_cmd_plug_detect_resp_t* out_state) const {
  ZX_DEBUG_ASSERT(out_state != nullptr);
  auto prop = fidl::WireCall(stream_ch_)->GetProperties();
  if (prop.status() != ZX_OK) {
    printf("Failed to get properties to watch plug state (res %d)\n", prop.status());
    return prop.status();
  }

  auto state = fidl::WireCall(stream_ch_)->WatchPlugState();
  if (state.status() != ZX_OK) {
    printf("Failed to watch plug state (res %d)\n", state.status());
    return state.status();
  }

  if (prop.value().properties.plug_detect_capabilities() ==
      audio_fidl::wire::PlugDetectCapabilities::kCanAsyncNotify) {
    out_state->plug_state_time = state.value().plug_state.plug_state_time();
    out_state->flags = state.value().plug_state.plugged() ? AUDIO_PDNF_PLUGGED : 0;
    out_state->flags |= AUDIO_PDNF_CAN_NOTIFY;
  } else {
    out_state->flags = AUDIO_PDNF_PLUGGED;
    out_state->flags |= AUDIO_PDNF_HARDWIRED;
  }
  return ZX_OK;
}

zx_status_t AudioDeviceStream::SetMute(bool mute) {
  muted_ = mute;
  return SetGainParams();
}

zx_status_t AudioDeviceStream::SetAgc(bool enabled) {
  agc_enabled_ = enabled;
  return SetGainParams();
}

zx_status_t AudioDeviceStream::SetGain(float gain) {
  gain_ = gain;
  return SetGainParams();
}

zx_status_t AudioDeviceStream::SetGainParams() {
  fidl::Arena allocator;
  audio_fidl::wire::GainState gain_state(allocator);
  gain_state.set_muted(muted_).set_agc_enabled(agc_enabled_).set_gain_db(gain_);
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)fidl::WireCall(stream_ch_)->SetGain(std::move(gain_state));
  return ZX_OK;
}

zx_status_t AudioDeviceStream::WatchGain(audio_stream_cmd_get_gain_resp_t* out_gain) const {
  auto prop = fidl::WireCall(stream_ch_)->GetProperties();
  if (prop.status() == ZX_OK) {
    out_gain->min_gain = prop.value().properties.min_gain_db();
    out_gain->max_gain = prop.value().properties.max_gain_db();
    out_gain->gain_step = prop.value().properties.gain_step_db();
  } else {
    printf("Failed to get properties to watch gainstate (res %d)\n", prop.status());
    return prop.status();
  }

  auto gain_state = fidl::WireCall(stream_ch_)->WatchGainState();
  if (gain_state.status() == ZX_OK) {
    out_gain->cur_gain = gain_state.value().gain_state.gain_db();
    out_gain->can_mute = gain_state.value().gain_state.has_muted();
    if (gain_state.value().gain_state.has_muted()) {
      out_gain->cur_mute = gain_state.value().gain_state.muted();
    }
    out_gain->can_agc = gain_state.value().gain_state.has_agc_enabled();
    if (gain_state.value().gain_state.has_agc_enabled()) {
      out_gain->cur_mute = gain_state.value().gain_state.agc_enabled();
    }
  } else {
    printf("Failed to watch gain state (res %d)\n", gain_state.status());
    return gain_state.status();
  }

  return ZX_OK;
}

zx_status_t AudioDeviceStream::GetUniqueId(audio_stream_cmd_get_unique_id_resp_t* out_id) const {
  if (out_id == nullptr)
    return ZX_ERR_INVALID_ARGS;
  auto result = fidl::WireCall(stream_ch_)->GetProperties();
  memcpy(out_id->unique_id.data, result.value().properties.unique_id().data(),
         std::min(result.value().properties.unique_id().size(), sizeof(out_id->unique_id)));
  return ZX_OK;
}

zx_status_t AudioDeviceStream::GetString(audio_stream_string_id_t id,
                                         audio_stream_cmd_get_string_resp_t* out_str) const {
  if (out_str == nullptr)
    return ZX_ERR_INVALID_ARGS;

  auto result = fidl::WireCall(stream_ch_)->GetProperties();
  switch (id) {
    case AUDIO_STREAM_STR_ID_MANUFACTURER:
      out_str->strlen = static_cast<uint32_t>(result.value().properties.manufacturer().size());
      memcpy(out_str->str, result.value().properties.manufacturer().data(), out_str->strlen);
      break;
    case AUDIO_STREAM_STR_ID_PRODUCT:
      out_str->strlen = static_cast<uint32_t>(result.value().properties.product().size());
      memcpy(out_str->str, result.value().properties.product().data(), out_str->strlen);
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t AudioDeviceStream::PlugMonitor(float duration, PlugMonitorCallback* monitor) {
  const double duration_ns = static_cast<double>(duration) * ZX_SEC(1);
  const zx_time_t deadline = zx_deadline_after(static_cast<zx_duration_t>(duration_ns));
  zx_time_t last_plug_time = zx::clock::get_monotonic().get();
  while (true) {
    // TODO(andresoportus): Currently if no plug state changes occur, we wait forever.
    // Once LLCPP supports async clients, stop monitoring even when there is no plug state change.
    audio_stream_cmd_plug_detect_resp_t out_state = {};
    auto status = WatchPlugState(&out_state);
    if (status != ZX_OK) {
      printf("Failed to watch plug state (res %d)\n", status);
      return status;
    }
    zx_time_t plug_time = out_state.plug_state_time;
    printf("Plug State now : %s (%.3lf sec since last change).\n",
           out_state.flags & AUDIO_PDNF_PLUGGED ? "plugged" : "unplugged",
           static_cast<double>(zx_time_sub_time(plug_time, last_plug_time)) /
               static_cast<double>(zx::sec(1).get()));

    if (out_state.flags & AUDIO_PDNF_HARDWIRED) {
      printf("Stream reports that it is hardwired, Monitoring is not possible.\n");
      return ZX_OK;
    }
    if (monitor) {
      return (*monitor)(out_state.flags & AUDIO_PDNF_PLUGGED, plug_time);
    }
    if (zx::clock::get_monotonic().get() > deadline) {
      break;
    }
  }
  printf("Monitoring finished.\n");
  return ZX_OK;
}

zx_status_t AudioDeviceStream::SetFormat(uint32_t frames_per_second, uint16_t channels,
                                         uint64_t channels_to_use_bitmask,
                                         audio_sample_format_t sample_format) {
  if (!stream_ch_.is_valid() || rb_ch_.is_valid())
    return ZX_ERR_BAD_STATE;

  auto formats = audio::utils::GetAllFormats(sample_format);
  ZX_ASSERT(formats.size() == 1);  // Must have one format.
  sample_size_ = formats[0].valid_bits_per_sample;
  channel_size_ = 8 * formats[0].bytes_per_sample;

  if (sample_size_ == 0 || channel_size_ == 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  channel_cnt_ = channels;
  frame_sz_ = channels * channel_size_ / 8;
  frame_rate_ = frames_per_second;
  sample_format_ = sample_format;

  auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }
  auto [local, remote] = std::move(endpoints.value());

  audio_fidl::wire::PcmFormat pcm_format = {};
  pcm_format.number_of_channels = static_cast<uint8_t>(channel_cnt_);
  pcm_format.sample_format = audio_fidl::wire::SampleFormat::kPcmSigned;
  pcm_format.frame_rate = frames_per_second;
  pcm_format.bytes_per_sample = channel_size_ / 8;
  pcm_format.valid_bits_per_sample = sample_size_;
  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  format.set_pcm_format(allocator, std::move(pcm_format));

  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)fidl::WireCall(stream_ch_)->CreateRingBuffer(std::move(format), std::move(remote));
  rb_ch_ = std::move(local);

  // Stash the FIFO depth, in case users need to know it.
  auto result = fidl::WireCall(rb_ch_)->GetProperties();
  if (result.status() != ZX_OK) {
    return ZX_ERR_BAD_STATE;
  }
  fifo_depth_ = result.value().properties.fifo_depth();
  if (result.value().properties.has_external_delay()) {
    external_delay_nsec_ = result.value().properties.external_delay();
  }

  // TODO(81650): Add support to audio-driver-ctl to interactively activate/deactivate channels.
  auto result2 = fidl::WireCall(rb_ch_)->SetActiveChannels(channels_to_use_bitmask);
  if (result2.status() == ZX_ERR_NOT_SUPPORTED) {
    printf("Active channel filtering not supported by the driver.\n");
    return ZX_OK;
  }
  return result2.status();
}

zx_status_t AudioDeviceStream::GetBuffer(uint32_t frames, uint32_t irqs_per_ring) {
  zx_status_t res;

  if (!frames)
    return ZX_ERR_INVALID_ARGS;

  if (!rb_ch_.is_valid() || rb_vmo_.is_valid() || !frame_sz_) {
    return ZX_ERR_BAD_STATE;
  }

  // Get a VMO representing the ring buffer we will share with the audio driver.
  // fast_capture_notifications not supported (set to an invalid channel).
  // fast_playback_notifications not supported (set to an invalid channel).
  auto result = fidl::WireCall(rb_ch_)->GetVmo(frames, irqs_per_ring);
  uint64_t rb_sz = static_cast<uint64_t>(result->value()->num_frames) * frame_sz_;
  rb_vmo_ = std::move(result->value()->ring_buffer);

  // We have the buffer, fetch the underlying size of the VMO (a rounded up
  // multiple of pages) and sanity check it against the effective size the
  // driver reported.
  uint64_t rb_page_sz;
  res = rb_vmo_.get_size(&rb_page_sz);
  if (res != ZX_OK) {
    printf("Failed to fetch ring buffer VMO size (res %d)\n", res);
    return res;
  }

  if ((rb_sz > std::numeric_limits<decltype(rb_sz_)>::max()) || (rb_sz > rb_page_sz)) {
    printf(
        "Bad ring buffer size returned by audio driver! "
        "(kernel size = %lu driver size = %lu)\n",
        rb_page_sz, rb_sz);
    return ZX_ERR_INVALID_ARGS;
  }

  rb_sz_ = static_cast<decltype(rb_sz_)>(rb_sz);

  // Map the VMO into our address space
  // TODO(johngro) : How do I specify the cache policy for this mapping?
  uint32_t flags = input() ? ZX_VM_PERM_READ : ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  res = zx::vmar::root_self()->map(flags, 0u, rb_vmo_, 0u, rb_sz_,
                                   reinterpret_cast<uintptr_t*>(&rb_virt_));

  if (res != ZX_OK) {
    printf("Failed to map ring buffer VMO (res %d)\n", res);
    return res;
  }

  // Success!  If this is an output device, zero out the buffer and we are done.
  if (!input()) {
    memset(rb_virt_, 0, rb_sz_);
  }

  return ZX_OK;
}

zx_status_t AudioDeviceStream::StartRingBuffer() {
  if (!rb_ch_.is_valid())
    return ZX_ERR_BAD_STATE;

  auto result = fidl::WireCall(rb_ch_)->Start();
  start_time_ = result.value().start_time;
  return result.status();
}

zx_status_t AudioDeviceStream::StopRingBuffer() {
  if (!rb_ch_.is_valid())
    return ZX_ERR_BAD_STATE;

  start_time_ = 0;
  auto result = fidl::WireCall(rb_ch_)->Stop();
  return result.status();
}

void AudioDeviceStream::ResetRingBuffer() {
  if (rb_virt_ != nullptr) {
    ZX_DEBUG_ASSERT(rb_sz_ != 0);
    zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(rb_virt_), rb_sz_);
  }
  rb_ch_.reset();
  rb_vmo_.reset();
  rb_sz_ = 0;
  rb_virt_ = nullptr;
}

void AudioDeviceStream::Close() {
  ResetRingBuffer();
  stream_ch_.reset();
}

bool AudioDeviceStream::IsChannelConnected(const zx::channel& ch) {
  if (!ch.is_valid())
    return false;

  zx_signals_t junk;
  return ch.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time(), &junk) != ZX_ERR_TIMED_OUT;
}

}  // namespace utils
}  // namespace audio
