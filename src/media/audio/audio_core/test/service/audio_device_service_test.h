// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_SERVICE_AUDIO_DEVICE_SERVICE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_SERVICE_AUDIO_DEVICE_SERVICE_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include "src/media/audio/lib/test/hermetic_audio_test.h"
#include "src/media/audio/lib/test/message_transceiver.h"

namespace media::audio::test {

class AudioDeviceServiceTest : public HermeticAudioTest {
 protected:
  void SetUp() override;
  void TearDown() override;

  void OnInboundStreamMessage(MessageTransceiver::Message message);

  void HandleCommandGetUniqueId(const audio_stream_cmd_get_unique_id_req_t& request);
  void HandleCommandGetString(const audio_stream_cmd_get_string_req_t& request);
  void HandleCommandGetGain(const audio_stream_cmd_get_gain_req_t& request);
  void HandleCommandGetFormats(const audio_stream_cmd_get_formats_req_t& request);
  void HandleCommandSetFormat(const audio_stream_cmd_set_format_req_t& request);

  void OnInboundRingBufferMessage(MessageTransceiver::Message message);

  void HandleCommandGetFifoDepth(audio_rb_cmd_get_fifo_depth_req_t& request);
  void HandleCommandGetBuffer(audio_rb_cmd_get_buffer_req_t& request);
  void HandleCommandStart(audio_rb_cmd_start_req_t& request);
  void HandleCommandStop(audio_rb_cmd_stop_req_t& request);

  void GetDevices();

  bool stream_config_complete() { return stream_config_complete_; }
  void set_stream_config_complete(bool complete) { stream_config_complete_ = complete; }

  std::vector<fuchsia::media::AudioDeviceInfo>& devices() { return devices_; }

  uint64_t device_token() { return device_token_; }
  void set_device_token(uint64_t token) { device_token_ = token; }

 private:
  fuchsia::media::AudioDeviceEnumeratorPtr audio_device_enumerator_;
  bool stream_config_complete_;
  std::vector<fuchsia::media::AudioDeviceInfo> devices_;
  uint64_t device_token_;

  MessageTransceiver stream_transceiver_;
  MessageTransceiver ring_buffer_transceiver_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_SERVICE_AUDIO_DEVICE_SERVICE_TEST_H_
