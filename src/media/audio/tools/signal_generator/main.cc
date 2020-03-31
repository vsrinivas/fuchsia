// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/tools/signal_generator/signal_generator.h"

namespace {
constexpr char kNumChannelsSwitch[] = "chans";
constexpr char kNumChannelsDefault[] = "2";
constexpr char kInt16FormatSwitch[] = "int16";
constexpr char kInt24FormatSwitch[] = "int24";
constexpr char kFrameRateSwitch[] = "rate";
constexpr char kFrameRateDefaultHz[] = "48000";

constexpr char kSineWaveSwitch[] = "sine";
constexpr char kSquareWaveSwitch[] = "square";
constexpr char kSawtoothWaveSwitch[] = "saw";
constexpr char kWhiteNoiseSwitch[] = "noise";
constexpr char kFrequencyDefaultHz[] = "440.0";

constexpr char kDurationSwitch[] = "dur";
constexpr char kDurationDefaultSecs[] = "2.0";
constexpr char kAmplitudeSwitch[] = "amp";
constexpr char kAmplitudeDefaultScale[] = "0.25";

constexpr char kSaveToFileSwitch[] = "wav";
constexpr char kSaveToFileDefaultName[] = "/tmp/signal_generator.wav";

constexpr char kFramesPerPayloadSwitch[] = "frames";
constexpr char kFramesPerPayloadDefault[] = "480";

constexpr char kNumPayloadBuffersSwitch[] = "num-bufs";
constexpr char kNumPayloadBuffersDefault[] = "1";

constexpr char kUsePtsSwitch[] = "pts";
constexpr char kPtsContinuityThresholdSwitch[] = "threshold";
constexpr char kPtsContinuityThresholdDefaultSecs[] = "0.0";

constexpr char kStreamGainSwitch[] = "gain";
constexpr char kStreamGainDefaultDb[] = "0.0";
constexpr char kStreamMuteSwitch[] = "mute";
constexpr char kStreamMuteDefault[] = "1";

constexpr char kStreamRampSwitch[] = "ramp";
constexpr char kStreamRampDurationSwitch[] = "ramp-dur";
constexpr char kStreamRampTargetGainSwitch[] = "end-gain";
constexpr char kStreamRampTargetGainDefaultDb[] = "-75.0";

constexpr char kRenderUsageSwitch[] = "usage";
constexpr char kRenderUsageDefault[] = "MEDIA";

constexpr char kRenderUsageGainSwitch[] = "usage-gain";
constexpr char kRenderUsageGainDefaultDb[] = "0.0";
constexpr char kRenderUsageVolumeSwitch[] = "usage-vol";
constexpr char kRenderUsageVolumeDefault[] = "0.50";

constexpr char kVerboseSwitch[] = "v";

constexpr char kHelpSwitch[] = "help";
constexpr char kHelp2Switch[] = "?";
}  // namespace

void usage(const char* prog_name) {
  printf("\nUsage: %s [--option] [...]\n", prog_name);
  printf("Generate and play an audio signal to the preferred output device.\n");
  printf("\nValid options:\n");

  printf("\n    By default, stream format is %s-channel, float32, %s Hz frame rate, %s usage\n",
         kNumChannelsDefault, kFrameRateDefaultHz, kRenderUsageDefault);
  printf("  --%s=<NUM_CHANS>\t Specify number of channels\n", kNumChannelsSwitch);
  printf("  --%s\t\t Use 16-bit integer samples\n", kInt16FormatSwitch);
  printf("  --%s\t\t Use 24-in-32-bit integer samples (left-justified 'padded-24')\n",
         kInt24FormatSwitch);
  printf("  --%s=<FRAME_RATE>\t Set frame rate in Hz\n", kFrameRateSwitch);
  printf("  --%s=<RENDER_USAGE> Set stream render usage. RENDER_USAGE must be one of:\n\t\t\t ",
         kRenderUsageSwitch);
  for (auto it = kRenderUsageOptions.cbegin(); it != kRenderUsageOptions.cend(); ++it) {
    printf("%s", it->first);
    if (it + 1 != kRenderUsageOptions.cend()) {
      printf(", ");
    } else {
      printf("\n");
    }
  }

  printf("\n    By default, signal is a sine wave. If no frequency is provided, %s Hz is used\n",
         kFrequencyDefaultHz);
  printf("  --%s[=<FREQ>]  \t Play sine wave at given frequency (Hz)\n", kSineWaveSwitch);
  printf("  --%s[=<FREQ>]  \t Play square wave at given frequency\n", kSquareWaveSwitch);
  printf("  --%s[=<FREQ>]  \t Play rising sawtooth wave at given frequency\n", kSawtoothWaveSwitch);
  printf("  --%s  \t\t Play pseudo-random 'white' noise\n", kWhiteNoiseSwitch);

  printf("\n    By default, play signal for %s seconds, at amplitude %s\n", kDurationDefaultSecs,
         kAmplitudeDefaultScale);
  printf("  --%s=<DURATION_SECS>\t Set playback length, in seconds\n", kDurationSwitch);
  printf("  --%s=<AMPL>\t\t Set amplitude (silence=0.0, full-scale=1.0)\n", kAmplitudeSwitch);

  printf("\n  --%s[=<FILEPATH>]\t Save to .wav file (default '%s')\n", kSaveToFileSwitch,
         kSaveToFileDefaultName);

  printf("\n    Subsequent settings (e.g. gain, timestamps) do not affect .wav file contents\n");

  printf("\n    By default, submit data in non-timestamped buffers of %s frames and %s VMOs.\n",
         kFramesPerPayloadDefault, kNumPayloadBuffersDefault);
  printf("  --%s\t\t\t Apply presentation timestamps (units: frames)\n", kUsePtsSwitch);
  printf("  --%s[=<SECS>]\t Set PTS discontinuity threshold, in seconds (default %s)\n",
         kPtsContinuityThresholdSwitch, kPtsContinuityThresholdDefaultSecs);
  printf("  --%s=<FRAMES>\t Set packet size, in frames \n", kFramesPerPayloadSwitch);
  printf("  --%s=<BUFFERS>\t Set the number of payload buffers to use \n",
         kNumPayloadBuffersSwitch);

  printf(
      "\n    By default, do not set AudioRenderer gain/mute (unity %.1f dB, unmuted, no ramping)\n",
      kUnityGainDb);
  printf("  --%s[=<GAIN_DB>]\t Set stream gain, in dB (min %.1f, max %.1f, default %s)\n",
         kStreamGainSwitch, fuchsia::media::audio::MUTED_GAIN_DB,
         fuchsia::media::audio::MAX_GAIN_DB, kStreamGainDefaultDb);
  printf("  --%s[=<0|1>]\t Set stream mute (0=Unmute or 1=Mute; Mute if only '--%s' is provided)\n",
         kStreamMuteSwitch, kStreamMuteSwitch);
  printf("  --%s\t\t Smoothly ramp gain from initial value to target %s dB by end-of-signal\n",
         kStreamRampSwitch, kStreamRampTargetGainDefaultDb);
  printf("\t\t\t If '--%s' is not provided, ramping starts at unity stream gain (%.1f dB)\n",
         kStreamGainSwitch, kUnityGainDb);
  printf("  --%s=<END_DB>\t Set a different ramp target gain (dB). Implies '--%s'\n",
         kStreamRampTargetGainSwitch, kStreamRampSwitch);
  printf("  --%s=<MSECS>\t Set a specific ramp duration in milliseconds. Implies '--%s'\n",
         kStreamRampDurationSwitch, kStreamRampSwitch);

  printf("\n    By default, both volume and gain for this RENDER_USAGE are unchanged\n");
  printf("  --%s[=<DB>]\t Set render usage gain, in dB (min %.1f, max %.1f, default %s)\n",
         kRenderUsageGainSwitch, fuchsia::media::audio::MUTED_GAIN_DB, kUnityGainDb,
         kRenderUsageGainDefaultDb);
  printf("  --%s[=<VOLUME>] Set render usage volume (min %.1f, max %.1f, default %s)\n",
         kRenderUsageVolumeSwitch, fuchsia::media::audio::MIN_VOLUME,
         fuchsia::media::audio::MAX_VOLUME, kRenderUsageVolumeDefault);

  printf("\n  --%s\t\t\t Display per-packet information\n", kVerboseSwitch);

  printf("  --%s, --%s\t\t Show this message\n\n", kHelpSwitch, kHelp2Switch);
}

int main(int argc, const char** argv) {
  syslog::InitLogger({"signal_generator"});

  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (command_line.HasOption(kHelpSwitch) || command_line.HasOption(kHelp2Switch)) {
    usage(argv[0]);
    return 0;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  media::tools::MediaApp media_app(
      [&loop]() { async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); }); });

  if (command_line.HasOption(kVerboseSwitch)) {
    media_app.set_verbose(true);
  }

  // Handle channels and frame-rate
  std::string num_channels_str =
      command_line.GetOptionValueWithDefault(kNumChannelsSwitch, kNumChannelsDefault);
  media_app.set_num_channels(fxl::StringToNumber<uint32_t>(num_channels_str));

  std::string frame_rate_str =
      command_line.GetOptionValueWithDefault(kFrameRateSwitch, kFrameRateDefaultHz);
  media_app.set_frame_rate(fxl::StringToNumber<uint32_t>(frame_rate_str));

  // Handle signal format
  if (command_line.HasOption(kInt16FormatSwitch)) {
    // Don't allow the user to specify more than one container format
    if (command_line.HasOption(kInt24FormatSwitch)) {
      usage(argv[0]);
      return 0;
    }
    media_app.set_int16_format(true);
  }

  if (command_line.HasOption(kInt24FormatSwitch)) {
    media_app.set_int24_format(true);
  }

  if (command_line.HasOption(kRenderUsageSwitch)) {
    std::string usage_option;
    command_line.GetOptionValue(kRenderUsageSwitch, &usage_option);
    auto it = std::find_if(kRenderUsageOptions.cbegin(), kRenderUsageOptions.cend(),
                           [&usage_option](auto usage_string_and_usage) {
                             return usage_option == usage_string_and_usage.first;
                           });
    if (it == kRenderUsageOptions.cend()) {
      printf("Unrecognized AudioRenderUsage %s\n\n", usage_option.c_str());
      usage(argv[0]);
      return 0;
    }
    media_app.set_usage(it->second);
  }

  // Handle signal type and frequency specifications.
  // If >1 type is specified, obey usage order: sine, square, saw, noise.
  std::string frequency_str = "";
  if (command_line.HasOption(kSineWaveSwitch)) {
    media_app.set_output_type(kOutputTypeSine);
    command_line.GetOptionValue(kSineWaveSwitch, &frequency_str);
  } else if (command_line.HasOption(kSquareWaveSwitch)) {
    media_app.set_output_type(kOutputTypeSquare);
    command_line.GetOptionValue(kSquareWaveSwitch, &frequency_str);
  } else if (command_line.HasOption(kSawtoothWaveSwitch)) {
    media_app.set_output_type(kOutputTypeSawtooth);
    command_line.GetOptionValue(kSawtoothWaveSwitch, &frequency_str);
  } else if (command_line.HasOption(kWhiteNoiseSwitch)) {
    media_app.set_output_type(kOutputTypeNoise);
  } else {
    media_app.set_output_type(kOutputTypeSine);
  }
  if (frequency_str == "") {
    frequency_str = kFrequencyDefaultHz;
  }

  media_app.set_frequency(std::stod(frequency_str));

  // Handle duration and amplitude of generated signal
  std::string amplitude_str =
      command_line.GetOptionValueWithDefault(kAmplitudeSwitch, kAmplitudeDefaultScale);
  if (amplitude_str != "") {
    media_app.set_amplitude(std::stof(amplitude_str));
  }

  std::string duration_str =
      command_line.GetOptionValueWithDefault(kDurationSwitch, kDurationDefaultSecs);
  if (duration_str != "") {
    media_app.set_duration(std::stod(duration_str));
  }

  // Handle payload buffer size
  std::string frames_per_payload_str =
      command_line.GetOptionValueWithDefault(kFramesPerPayloadSwitch, kFramesPerPayloadDefault);
  media_app.set_frames_per_payload(fxl::StringToNumber<uint32_t>(frames_per_payload_str));

  // Set the number of buffers to use.
  std::string num_payload_buffers_str =
      command_line.GetOptionValueWithDefault(kNumPayloadBuffersSwitch, kNumPayloadBuffersDefault);
  media_app.set_num_payload_buffers(fxl::StringToNumber<uint32_t>(num_payload_buffers_str));

  // Handle timestamp usage
  media_app.set_use_pts(command_line.HasOption(kUsePtsSwitch));
  if (command_line.HasOption(kPtsContinuityThresholdSwitch)) {
    std::string pts_continuity_threshold_str;
    command_line.GetOptionValue(kPtsContinuityThresholdSwitch, &pts_continuity_threshold_str);
    if (pts_continuity_threshold_str == "") {
      pts_continuity_threshold_str = kPtsContinuityThresholdDefaultSecs;
    }
    media_app.set_pts_continuity_threshold(std::stof(pts_continuity_threshold_str));
  }

  // Handle stream gain
  if (command_line.HasOption(kStreamGainSwitch)) {
    std::string stream_gain_str;
    command_line.GetOptionValue(kStreamGainSwitch, &stream_gain_str);
    if (stream_gain_str == "") {
      stream_gain_str = kStreamGainDefaultDb;
    }

    media_app.set_stream_gain(std::stof(stream_gain_str));
  }

  if (command_line.HasOption(kStreamMuteSwitch)) {
    std::string stream_mute_str;
    command_line.GetOptionValue(kStreamMuteSwitch, &stream_mute_str);
    if (stream_mute_str == "") {
      stream_mute_str = kStreamMuteDefault;
    }

    media_app.set_stream_mute(fxl::StringToNumber<uint32_t>(stream_mute_str) != 0);
  }

  // Handle stream gain ramping, target gain and ramp duration.
  if (command_line.HasOption(kStreamRampSwitch) ||
      command_line.HasOption(kStreamRampTargetGainSwitch) ||
      command_line.HasOption(kStreamRampDurationSwitch)) {
    media_app.set_will_ramp_stream_gain();

    std::string target_gain_db_str = command_line.GetOptionValueWithDefault(
        kStreamRampTargetGainSwitch, kStreamRampTargetGainDefaultDb);
    if (target_gain_db_str == "") {
      target_gain_db_str = kStreamRampTargetGainDefaultDb;
    }
    media_app.set_ramp_target_gain_db(std::stof(target_gain_db_str));

    // Convert signal duration of doublefloat seconds, to int64 nanoseconds.
    auto ramp_duration_nsec = static_cast<zx_duration_t>(media_app.get_duration() * 1000000000.0);
    if (command_line.HasOption(kStreamRampDurationSwitch)) {
      std::string ramp_duration_str = "";
      command_line.GetOptionValue(kStreamRampDurationSwitch, &ramp_duration_str);

      if (ramp_duration_str != "") {
        // Convert input of doublefloat milliseconds, to int64 nanoseconds.
        ramp_duration_nsec = static_cast<zx_duration_t>(std::stod(ramp_duration_str) * 1000000.0);
      }
    }
    media_app.set_ramp_duration_nsec(ramp_duration_nsec);
  }

  // Handle render usage volume and gain
  if (command_line.HasOption(kRenderUsageVolumeSwitch)) {
    std::string usage_volume_str;
    command_line.GetOptionValue(kRenderUsageVolumeSwitch, &usage_volume_str);
    if (usage_volume_str == "") {
      usage_volume_str = kRenderUsageVolumeDefault;
    }

    media_app.set_usage_volume(std::stof(usage_volume_str));
  }
  if (command_line.HasOption(kRenderUsageGainSwitch)) {
    std::string usage_gain_str;
    command_line.GetOptionValue(kRenderUsageGainSwitch, &usage_gain_str);
    if (usage_gain_str == "") {
      usage_gain_str = kRenderUsageGainDefaultDb;
    }

    media_app.set_usage_gain(std::stof(usage_gain_str));
  }

  // Handle "generate to file"
  if (command_line.HasOption(kSaveToFileSwitch)) {
    std::string save_file_str;
    command_line.GetOptionValue(kSaveToFileSwitch, &save_file_str);

    // If just '--wav' is specified, use the default file name.
    if (save_file_str == "") {
      save_file_str = kSaveToFileDefaultName;
    }

    media_app.set_save_to_file(true);
    media_app.set_save_file_name(save_file_str);
  }

  media_app.Run(component_context.get());

  // We've set everything going. Wait for our message loop to return.
  loop.Run();

  return 0;
}
