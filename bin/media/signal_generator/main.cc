// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/media/signal_generator/signal_generator.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/strings/string_number_conversions.h"

namespace {
constexpr char kNumChannelsSwitch[] = "chans";
constexpr char kNumChannelsDefault[] = "2";

constexpr char kFrameRateSwitch[] = "rate";
constexpr char kFrameRateDefaultHz[] = "48000";
constexpr char kInt16FormatSwitch[] = "int16";
constexpr char kInt24FormatSwitch[] = "int24";

constexpr char kSineWaveSwitch[] = "sine";
constexpr char kSquareWaveSwitch[] = "square";
constexpr char kSawtoothWaveSwitch[] = "saw";
constexpr char kWhiteNoiseSwitch[] = "noise";
constexpr char kFrequencyDefaultHz[] = "440.0";

constexpr char kAmplitudeSwitch[] = "amp";
constexpr char kAmplitudeDefaultScale[] = "0.5";

constexpr char kDurationSwitch[] = "dur";
constexpr char kDurationDefaultSecs[] = "2.0";
constexpr char kFramesPerPayloadSwitch[] = "frames";
constexpr char kFramesPerPayloadDefault[] = "480";

constexpr char kSaveToFileSwitch[] = "wav";
constexpr char kSaveToFileDefaultName[] = "/tmp/signal_generator.wav";

constexpr char kStreamGainSwitch[] = "gain";
constexpr char kStreamGainDefaultDb[] = "0.0";
constexpr char kStreamRampSwitch[] = "ramp";
constexpr char kStreamRampDurationSwitch[] = "rampdur";
constexpr char kStreamRampTargetGainSwitch[] = "endgain";
constexpr char kStreamRampTargetGainDefaultDb[] = "-75.0";

constexpr char kSystemGainSwitch[] = "sgain";
constexpr char kSystemGainDefaultDb[] = "-12.0";
constexpr char kSystemMuteSwitch[] = "smute";
constexpr char kSystemMuteDefault[] = "1";

constexpr char kPlayToLastSwitch[] = "last";
constexpr char kPlayToAllSwitch[] = "all";

constexpr char kHelpSwitch[] = "help";
}  // namespace

void usage(const char* prog_name) {
  printf("\nUsage: %s [--option] [...]\n", prog_name);
  printf("Generate and play an audio signal to the preferred output device.\n");
  printf("\nAdditional optional settings include:\n");

  printf("\t--%s=<NUM_CHANS>\tSpecify number of output channels (default %s)\n",
         kNumChannelsSwitch, kNumChannelsDefault);
  printf("\t--%s=<FRAME_RATE>\tSet output frame rate in Hertz (default %s)\n",
         kFrameRateSwitch, kFrameRateDefaultHz);
  printf("\t--%s\t\t\tEmit signal as 16-bit integer (default float32)\n",
         kInt16FormatSwitch);
  printf("\t--%s\t\t\tEmit signal as 24-in-32-bit integer (default float32)\n",
         kInt24FormatSwitch);

  printf(
      "\n\t--%s[=<FREQ>]  \tPlay sine of given frequency, in Hz (default %s)\n",
      kSineWaveSwitch, kFrequencyDefaultHz);
  printf("\t--%s[=<FREQ>]  \tPlay square wave (default %s Hz)\n",
         kSquareWaveSwitch, kFrequencyDefaultHz);
  printf("\t--%s[=<FREQ>]  \tPlay rising sawtooth wave (default %s Hz)\n",
         kSawtoothWaveSwitch, kFrequencyDefaultHz);
  printf("\t--%s  \t\tPlay pseudo-random 'white' noise\n", kWhiteNoiseSwitch);
  printf("\t\t\t\tIn the absence of any of the above, a sine is played.\n");

  printf(
      "\n\t--%s=<AMPL>\t\tSet signal amplitude (full-scale=1.0, default %s)\n",
      kAmplitudeSwitch, kAmplitudeDefaultScale);
  printf(
      "\n\t--%s=<DURATION_SEC>\tSet playback length, in seconds (default %s)\n",
      kDurationSwitch, kDurationDefaultSecs);
  printf("\t--%s=<FRAMES>\tSet data buffer size, in frames (default %s)\n",
         kFramesPerPayloadSwitch, kFramesPerPayloadDefault);

  printf("\n\t--%s[=<FILEPATH>]\tSave signal to .wav file (default %s)\n",
         kSaveToFileSwitch, kSaveToFileDefaultName);
  printf("\t\t\t\tGain/mute/ramp settings do not affect .wav file contents.");
  printf("\n\t\t\t\t24-bit signals are saved left-justified in 32-bit ints.\n");

  printf(
      "\n\t--%s[=<GAIN_DB>]\tSet AudioRenderer gain, in dB (default %s, "
      "range %.1f, %.1f])\n",
      kStreamGainSwitch, kStreamGainDefaultDb, fuchsia::media::MUTED_GAIN_DB,
      fuchsia::media::MAX_GAIN_DB);
  printf("\t\t\t\tIf '--%s' is not specified, stream plays at unity gain.\n",
         kStreamGainSwitch);

  printf(
      "\n\t--%s\t\t\tSmoothly ramp gain to %s dB, over the signal duration.\n",
      kStreamRampSwitch, kStreamRampTargetGainDefaultDb);
  printf(
      "\t\t\t\tValues for end-of-ramp gain and ramp duration are overridden\n"
      "\t\t\t\tby '--%s' and '--%s', respectively.\n",
      kStreamRampTargetGainSwitch, kStreamRampDurationSwitch);
  printf("\t\t\t\tIf '--%s' is not specified, ramping starts at unity gain.\n",
         kStreamGainSwitch);
  printf(
      "\t--%s=<GAIN_DB>\tRamp stream gain to the specified value (in dB),\n"
      "\t\t\t\tinstead of the default %s dB. (implies '--%s')\n",
      kStreamRampTargetGainSwitch, kStreamRampTargetGainDefaultDb,
      kStreamRampSwitch);
  printf(
      "\t--%s=<DURATION_MS>\tRamp stream gain over the specified duration "
      "(in milliseconds),\n",
      kStreamRampDurationSwitch);
  printf("\t\t\t\trather than the signal's entire length. (implies '--%s')\n",
         kStreamRampSwitch);

  printf("\n\t--%s=<GAIN>\t\tSet System Gain to [%.1f, 0.0] dB (default %s)\n",
         kSystemGainSwitch, fuchsia::media::MUTED_GAIN_DB,
         kSystemGainDefaultDb);
  printf("\t--%s[=<0|1>]\t\tSet System Mute (1=mute, 0=unmute, default %s)\n",
         kSystemMuteSwitch, kSystemMuteDefault);
  printf("\t\t\t\tNote: changes to System Gain/Mute persist after playback.\n");

  printf("\n\t--%s\t\t\tSet 'Play to Most-Recently-Plugged' policy\n",
         kPlayToLastSwitch);
  printf("\t--%s\t\t\tSet 'Play to All' policy\n", kPlayToAllSwitch);
  printf("\t\t\t\tNote: changes to audio policy persist after playback.\n");

  printf("\n\t--%s, --?\t\tShow this message\n\n", kHelpSwitch);
}

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (command_line.HasOption("?") || command_line.HasOption(kHelpSwitch)) {
    usage(argv[0]);
    return 0;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_context = component::StartupContext::CreateFromStartupInfo();

  media::tools::MediaApp media_app([&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });

  // Handle channels and frame-rate
  std::string num_channels_str = command_line.GetOptionValueWithDefault(
      kNumChannelsSwitch, kNumChannelsDefault);
  media_app.set_num_channels(fxl::StringToNumber<uint32_t>(num_channels_str));

  std::string frame_rate_str = command_line.GetOptionValueWithDefault(
      kFrameRateSwitch, kFrameRateDefaultHz);
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

  // Handle signal type and frequency specifications.
  // If >1 type is specified, obey usage order: sine, square, saw, noise.
  std::string frequency_str = "";
  if (command_line.HasOption(kSineWaveSwitch)) {
    media_app.set_output_type(media::tools::kOutputTypeSine);
    command_line.GetOptionValue(kSineWaveSwitch, &frequency_str);
  } else if (command_line.HasOption(kSquareWaveSwitch)) {
    media_app.set_output_type(media::tools::kOutputTypeSquare);
    command_line.GetOptionValue(kSquareWaveSwitch, &frequency_str);
  } else if (command_line.HasOption(kSawtoothWaveSwitch)) {
    media_app.set_output_type(media::tools::kOutputTypeSawtooth);
    command_line.GetOptionValue(kSawtoothWaveSwitch, &frequency_str);
  } else if (command_line.HasOption(kWhiteNoiseSwitch)) {
    media_app.set_output_type(media::tools::kOutputTypeNoise);
  } else {
    media_app.set_output_type(media::tools::kOutputTypeSine);
  }
  if (frequency_str == "") {
    frequency_str = kFrequencyDefaultHz;
  }

  media_app.set_frequency(std::stod(frequency_str));

  // Handle duration and amplitude of generated signal
  std::string amplitude_str = command_line.GetOptionValueWithDefault(
      kAmplitudeSwitch, kAmplitudeDefaultScale);
  if (amplitude_str != "") {
    media_app.set_amplitude(std::stof(amplitude_str));
  }

  std::string duration_str = command_line.GetOptionValueWithDefault(
      kDurationSwitch, kDurationDefaultSecs);
  if (duration_str != "") {
    media_app.set_duration(std::stod(duration_str));
  }

  // Handle payload buffer size
  std::string frames_per_payload_str = command_line.GetOptionValueWithDefault(
      kFramesPerPayloadSwitch, kFramesPerPayloadDefault);
  media_app.set_frames_per_payload(
      fxl::StringToNumber<uint32_t>(frames_per_payload_str));

  // Handle stream gain
  if (command_line.HasOption(kStreamGainSwitch)) {
    std::string stream_gain_str;
    command_line.GetOptionValue(kStreamGainSwitch, &stream_gain_str);
    if (stream_gain_str == "") {
      stream_gain_str = kStreamGainDefaultDb;
    }

    media_app.set_will_set_stream_gain(true);
    media_app.set_stream_gain(std::stof(stream_gain_str));
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
    zx_duration_t ramp_duration_nsec =
        static_cast<zx_duration_t>(media_app.get_duration() * 1000000000.0);
    if (command_line.HasOption(kStreamRampDurationSwitch)) {
      std::string ramp_duration_str = "";
      command_line.GetOptionValue(kStreamRampDurationSwitch,
                                  &ramp_duration_str);

      if (ramp_duration_str != "") {
        // Convert input of doublefloat milliseconds, to int64 nanoseconds.
        ramp_duration_nsec = static_cast<zx_duration_t>(
            std::stod(ramp_duration_str) * 1000000.0);
      }
    }
    media_app.set_ramp_duration_nsec(ramp_duration_nsec);
  }

  // Handle system gain and system mute
  if (command_line.HasOption(kSystemGainSwitch)) {
    media_app.set_will_set_system_gain();

    std::string system_gain_str;
    command_line.GetOptionValue(kSystemGainSwitch, &system_gain_str);
    if (system_gain_str == "") {
      system_gain_str = kSystemGainDefaultDb;
    }

    media_app.set_system_gain(std::stof(system_gain_str));
  }

  if (command_line.HasOption(kSystemMuteSwitch)) {
    std::string system_mute_str;
    command_line.GetOptionValue(kSystemMuteSwitch, &system_mute_str);
    if (system_mute_str == "") {
      system_mute_str = kSystemMuteDefault;
    }

    if (fxl::StringToNumber<uint32_t>(system_mute_str) != 0) {
      media_app.set_system_mute();
    } else {
      media_app.set_system_unmute();
    }
  }

  // Handle output routing policy
  if (command_line.HasOption(kPlayToLastSwitch)) {
    // Don't allow the user to specify both policies
    if (command_line.HasOption(kPlayToAllSwitch)) {
      usage(argv[0]);
      return 0;
    }
    media_app.set_will_set_audio_policy(true);
    media_app.set_audio_policy(
        fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT);
  }
  if (command_line.HasOption(kPlayToAllSwitch)) {
    media_app.set_will_set_audio_policy(true);
    media_app.set_audio_policy(
        fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);
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

  media_app.Run(startup_context.get());

  // We've set everything going. Wait for our message loop to return.
  loop.Run();

  return 0;
}
