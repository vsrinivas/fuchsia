// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DRIVER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DRIVER_H_

#include <lib/zx/channel.h>
#include <zircon/device/audio.h>

#include <cstring>

#include "src/media/audio/audio_core/object_registry.h"
#include "src/media/audio/lib/test/message_transceiver.h"

namespace media::audio::testing {

class FakeAudioDriver {
 public:
  FakeAudioDriver(zx::channel channel);

  void set_stream_unique_id(const audio_stream_unique_id_t& uid) {
    std::memcpy(uid_.data, uid.data, sizeof(uid.data));
  }
  void set_device_manufacturer(std::string mfgr) { manufacturer_ = std::move(mfgr); }
  void set_device_product(std::string product) { product_ = std::move(product); }
  void set_gain(float gain) { cur_gain_ = gain; }
  void set_gain_limits(float min_gain, float max_gain) {
    gain_limits_ = std::make_pair(min_gain, max_gain);
  }
  void set_can_agc(bool can_agc) { can_agc_ = can_agc; }
  void set_cur_agc(bool cur_agc) { cur_agc_ = cur_agc; }
  void set_can_mute(bool can_mute) { can_mute_ = can_mute; }
  void set_cur_mute(bool cur_mute) { cur_mute_ = cur_mute; }
  void set_formats(std::vector<audio_stream_format_range_t> formats) {
    formats_ = std::move(formats);
  }

 private:
  void OnInboundStreamMessage(test::MessageTransceiver::Message message);
  void OnInboundStreamError(zx_status_t status);
  void HandleCommandGetUniqueId(const audio_stream_cmd_get_unique_id_req_t& request);
  void HandleCommandGetString(const audio_stream_cmd_get_string_req_t& request);
  void HandleCommandGetGain(const audio_stream_cmd_get_gain_req_t& request);
  void HandleCommandGetFormats(const audio_stream_cmd_get_formats_req_t& request);
  void HandleCommandSetFormat(const audio_stream_cmd_set_format_req_t& request);

  void OnInboundRingBufferMessage(test::MessageTransceiver::Message message);
  void OnInboundRingBufferError(zx_status_t status);
  void HandleCommandGetFifoDepth(audio_rb_cmd_get_fifo_depth_req_t& request);
  void HandleCommandGetBuffer(audio_rb_cmd_get_buffer_req_t& request);
  void HandleCommandStart(audio_rb_cmd_start_req_t& request);
  void HandleCommandStop(audio_rb_cmd_stop_req_t& request);

  audio_stream_unique_id_t uid_ = {};
  std::string manufacturer_ = "default manufacturer";
  std::string product_ = "default product";
  float cur_gain_ = 0.0f;
  std::pair<float, float> gain_limits_{-160.0f, 3.0f};
  bool can_agc_ = true;
  bool cur_agc_ = false;
  bool can_mute_ = true;
  bool cur_mute_ = false;
  std::vector<audio_stream_format_range_t> formats_{{
      .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
      .min_frames_per_second = 48000,
      .max_frames_per_second = 48000,
      .min_channels = 2,
      .max_channels = 2,
      .flags = ASF_RANGE_FLAG_FPS_48000_FAMILY,
  }};

  test::MessageTransceiver stream_transceiver_;
  test::MessageTransceiver ring_buffer_transceiver_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DRIVER_H_
