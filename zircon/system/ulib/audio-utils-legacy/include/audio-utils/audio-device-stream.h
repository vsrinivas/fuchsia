// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUDIO_UTILS_AUDIO_DEVICE_STREAM_H_
#define AUDIO_UTILS_AUDIO_DEVICE_STREAM_H_

#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/device/audio.h>
#include <zircon/types.h>

#include <functional>

#include <fbl/vector.h>

namespace audio {
namespace utils {

class AudioDeviceStream {
 public:
  enum class StreamDirection {
    kInput,
    kOutput,
  };
  using PlugMonitorCallback = std::function<bool(bool plug_state, zx_time_t plug_time)>;

  zx_status_t Open();
  zx_status_t GetSupportedFormats(fbl::Vector<audio_stream_format_range_t>* out_formats) const;
  zx_status_t SetMute(bool mute);
  zx_status_t SetAgc(bool enabled);
  zx_status_t SetGain(float gain);
  zx_status_t GetGain(audio_stream_cmd_get_gain_resp_t* out_gain) const;
  zx_status_t GetUniqueId(audio_stream_cmd_get_unique_id_resp_t* out_id) const;
  zx_status_t GetString(audio_stream_string_id_t id,
                        audio_stream_cmd_get_string_resp_t* out_str) const;
  zx_status_t GetClockDomain(audio_stream_cmd_get_clock_domain_resp_t* out_clk_domain) const;
  zx_status_t PlugMonitor(float duration, PlugMonitorCallback* monitor);
  zx_status_t SetFormat(uint32_t frames_per_second, uint16_t channels,
                        audio_sample_format_t sample_format);
  zx_status_t GetBuffer(uint32_t frames, uint32_t irqs_per_ring);
  zx_status_t StartRingBuffer();
  zx_status_t StopRingBuffer();
  void ResetRingBuffer();
  void Close();

  zx_status_t GetPlugState(audio_stream_cmd_plug_detect_resp_t* out_state) const {
    return GetPlugState(out_state, false);
  }

  bool IsStreamBufChannelConnected() const { return IsChannelConnected(stream_ch_); }
  bool IsRingBufChannelConnected() const { return IsChannelConnected(rb_ch_); }
  void SetStreamChannel(zx::channel channel) { stream_ch_ = std::move(channel); }
  zx::unowned_channel BorrowRingBufferChannel() { return zx::unowned_channel(rb_ch_); }

  const char* name() const { return name_; }
  bool input() const { return direction_ == StreamDirection::kInput; }
  uint32_t frame_rate() const { return frame_rate_; }
  uint32_t sample_size() const { return sample_size_; }
  uint32_t channel_cnt() const { return channel_cnt_; }
  uint32_t frame_sz() const { return frame_sz_; }
  uint64_t fifo_depth() const { return fifo_depth_; }
  uint32_t ring_buffer_bytes() const { return rb_sz_; }
  void* ring_buffer() const { return rb_virt_; }
  uint64_t start_time() const { return start_time_; }
  uint64_t external_delay_nsec() const { return external_delay_nsec_; }
  zx_status_t GetPlugState(audio_stream_cmd_plug_detect_resp_t* out_state,
                           bool enable_notify) const;

 protected:
  friend class std::default_delete<AudioDeviceStream>;

  static bool IsChannelConnected(const zx::channel& ch);
  void DisablePlugNotifications();

  AudioDeviceStream(StreamDirection direction, uint32_t dev_id);
  AudioDeviceStream(StreamDirection direction, const char* dev_path);
  virtual ~AudioDeviceStream();

  zx::channel stream_ch_;
  zx::channel rb_ch_;
  zx::vmo rb_vmo_;

  const StreamDirection direction_;
  char name_[64] = {0};

  audio_sample_format_t sample_format_;
  uint64_t start_time_ = 0;
  uint64_t external_delay_nsec_ = 0;
  uint32_t frame_rate_ = 0;
  uint32_t sample_size_ = 0;
  uint32_t channel_cnt_ = 0;
  uint32_t frame_sz_ = 0;
  uint32_t fifo_depth_ = 0;
  uint32_t rb_sz_ = 0;
  void* rb_virt_ = nullptr;
};

}  // namespace utils
}  // namespace audio

#endif  // AUDIO_UTILS_AUDIO_DEVICE_STREAM_H_
