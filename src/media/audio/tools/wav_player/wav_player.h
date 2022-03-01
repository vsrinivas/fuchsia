// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_TOOLS_WAV_PLAYER_WAV_PLAYER_H_
#define SRC_MEDIA_AUDIO_TOOLS_WAV_PLAYER_WAV_PLAYER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/clock.h>

#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/media/audio/lib/wav/wav_reader.h"

constexpr float kUnityGainDb = 0.0f;

enum class ClockType { Default, Flexible };

constexpr std::array<std::pair<const char*, fuchsia::media::AudioRenderUsage>,
                     fuchsia::media::RENDER_USAGE_COUNT>
    kRenderUsageOptions = {{
        {"BACKGROUND", fuchsia::media::AudioRenderUsage::BACKGROUND},
        {"MEDIA", fuchsia::media::AudioRenderUsage::MEDIA},
        {"INTERRUPTION", fuchsia::media::AudioRenderUsage::INTERRUPTION},
        {"SYSTEM_AGENT", fuchsia::media::AudioRenderUsage::SYSTEM_AGENT},
        {"COMMUNICATION", fuchsia::media::AudioRenderUsage::COMMUNICATION},
    }};

// Any audio output device fed by the system audio mixer will have this min_lead_time, at least.
// Until then, we cannot be confident that our renderer is routed to an actual device.
// TODO(fxbug.dev/50117): remove the workaround once audio_core fixes the underlying fxbug.dev/50017
constexpr zx::duration kRealDeviceMinLeadTime = zx::msec(1);

namespace media::tools {
class WavPlayer {
 public:
  struct Options {
    fit::closure quit_callback;
    std::string file_name;
    bool loop_playback;
    bool ultrasound;

    uint32_t frames_per_packet;
    uint32_t frames_per_payload_buffer;

    ClockType clock_type;

    std::optional<fuchsia::media::AudioRenderUsage> usage;
    std::optional<float> usage_gain_db;
    std::optional<float> usage_volume;
    std::optional<float> stream_gain_db;
    std::optional<bool> stream_mute;

    bool verbose;
  };
  static constexpr fuchsia::media::AudioRenderUsage kDefaultUsage =
      fuchsia::media::AudioRenderUsage::MEDIA;

  explicit WavPlayer(Options options);

  void Run(sys::ComponentContext* app_context);
  void OnKeyPress();

 private:
  void AcquireRenderer(sys::ComponentContext* app_context);
  void InitializeWavReader();
  void ParameterRangeChecks();
  void SetupPayloadCoefficients();
  void DisplayConfigurationSettings();

  void ConfigureRenderer();
  void SetLoudnessLevels(sys::ComponentContext* app_context);
  void SetAudioRendererEvents();
  void CreateMemoryMapping();

  void GetClockAndStart();
  void Play();

  void SendPacket();
  bool CheckPayloadSpace();
  void OnSendPacketComplete(uint64_t frames_completed);

  fuchsia::media::StreamPacket CreateAudioPacket(uint64_t packet_num);
  uint64_t RetrieveAudioForPacket(const fuchsia::media::StreamPacket& packet, uint64_t packet_num);

  bool Shutdown();

  std::unique_ptr<media::audio::WavReader> wav_reader_;

  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::audio::GainControlPtr gain_control_;            // for stream-local gain
  fuchsia::media::audio::VolumeControlPtr usage_volume_control_;  // for usage volume

  uint32_t num_channels_;
  uint32_t frame_rate_;
  uint32_t frame_size_;
  fuchsia::media::AudioSampleFormat sample_format_ = fuchsia::media::AudioSampleFormat::FLOAT;

  zx::duration min_lead_time_;
  zx::clock reference_clock_;

  fzl::VmoMapper payload_buffer_;
  uint32_t bytes_per_packet_;
  uint32_t packets_per_payload_buffer_;
  uint64_t num_packets_sent_ = 0u;
  uint64_t num_packets_completed_ = 0u;
  uint64_t num_frames_completed_ = 0u;

  Options options_;

  bool started_ = false;
  bool stopping_ = false;
  bool looping_reached_end_of_file_ = false;

  fsl::FDWaiter keystroke_waiter_;
};

}  // namespace media::tools

#endif  // SRC_MEDIA_AUDIO_TOOLS_WAV_PLAYER_WAV_PLAYER_H_
