// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_SIGNAL_GENERATOR_SIGNAL_GENERATOR_H_
#define GARNET_BIN_MEDIA_SIGNAL_GENERATOR_SIGNAL_GENERATOR_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/vmo-utils/vmo_mapper.h>

#include "garnet/lib/media/wav_writer/wav_writer.h"
#include "lib/app/cpp/startup_context.h"

namespace media {
namespace tools {

typedef enum {
  kOutputTypeNoise,
  kOutputTypeSine,
  kOutputTypeSquare,
  kOutputTypeSawtooth,
} OutputSignalType;
// TODO(mpuryear): refactor the signal-generation section to make it easier for
// new generators to be added.

class MediaApp {
 public:
  MediaApp(fit::closure quit_callback);

  void set_num_channels(uint32_t num_channels) { num_channels_ = num_channels; }
  void set_frame_rate(uint32_t frame_rate) { frame_rate_ = frame_rate; }
  void set_int16_format(bool use_int16) { use_int16_ = use_int16; }

  void set_output_type(OutputSignalType output_type) {
    output_signal_type_ = output_type;
  }
  void set_frequency(uint32_t frequency) {
    frequency_ = static_cast<float>(frequency);
  }
  void set_amplitude(float amplitude) { amplitude_ = amplitude; }

  void set_duration(uint32_t duration_secs) { duration_secs_ = duration_secs; }
  void set_msec_per_payload(size_t msecs_per_payload) {
    msecs_per_payload_ = msecs_per_payload;
  }

  void set_save_to_file(bool save_to_file) { save_to_file_ = save_to_file; }
  void set_save_file_name(std::string file_name) { file_name_ = file_name; }

  void set_renderer_gain(float rend_gain) { renderer_gain_db_ = rend_gain; }
  void set_will_set_system_gain(bool set_system_gain) {
    set_system_gain_ = set_system_gain;
  }
  void set_system_gain(float system_gain) { system_gain_db_ = system_gain; }

  void set_will_set_audio_policy(bool set_policy) { set_policy_ = set_policy; }
  void set_audio_policy(fuchsia::media::AudioOutputRoutingPolicy policy) {
    audio_policy_ = policy;
  }

  void Run(fuchsia::sys::StartupContext* app_context);

 private:
  bool SetupPayloadCoefficients();
  void AcquireRenderer(fuchsia::sys::StartupContext* app_context);
  void SetMediaType();

  zx_status_t CreateMemoryMapping();

  template <typename SampleType>
  static void WriteAudioIntoBuffer(SampleType* audio_buffer,
                                   double frames_per_period, double amp_scalar,
                                   size_t num_frames, uint32_t num_chans,
                                   OutputSignalType signal_type);

  fuchsia::media::AudioPacket CreateAudioPacket(size_t packet_num);
  void SendPacket(fuchsia::media::AudioPacket packet);
  void OnSendPacketComplete();

  void Shutdown();

  fit::closure quit_callback_;

  fuchsia::media::AudioRenderer2Ptr audio_renderer_;

  vmo_utils::VmoMapper payload_buffer_;

  uint32_t num_channels_;
  uint32_t frame_rate_;
  bool use_int16_ = false;
  size_t sample_size_;

  OutputSignalType output_signal_type_;

  // TODO(mpuryear): need to make this a double (not an int).
  uint32_t frequency_;
  double frames_per_period_;

  double amplitude_;
  double amplitude_scalar_;

  // TODO(mpuryear): need to make this a double (not an int), so that playback
  // can be of arbitrary duration.
  uint32_t duration_secs_;

  // TODO(mpuryear): change the cmd-line parameter to directly accept
  // frames_per_payload from the user, so that packets can be of any length.
  uint32_t msecs_per_payload_;
  size_t frames_per_payload_;

  // TODO(mpuryear): need to generate the payloads on the fly instead of
  // pre-computing a second of audio and looping it.
  size_t payload_size_;
  size_t payload_mapping_size_;
  size_t num_payloads_;
  uint32_t num_packets_to_send_;
  size_t num_packets_sent_ = 0u;
  size_t num_packets_completed_ = 0u;

  bool save_to_file_ = false;
  std::string file_name_;
  media::audio::WavWriter<> wav_writer_;

  float renderer_gain_db_;
  bool set_system_gain_ = false;
  float system_gain_db_;

  bool set_policy_ = false;
  fuchsia::media::AudioOutputRoutingPolicy audio_policy_;
};

}  // namespace tools
}  // namespace media

#endif  // GARNET_BIN_MEDIA_SIGNAL_GENERATOR_SIGNAL_GENERATOR_H_
