// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/media/signal_generator/signal_generator.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/strings/string_number_conversions.h"

namespace {
constexpr char kNumChannelsSwitch[] = "chans";
constexpr char kNumChannelsDefault[] = "2";

constexpr char kFrameRateSwitch[] = "rate";
constexpr char kFrameRateDefaultHz[] = "48000";
constexpr char kInt16FormatSwitch[] = "int";

constexpr char kSineWaveSwitch[] = "sine";
constexpr char kSquareWaveSwitch[] = "square";
constexpr char kSawtoothWaveSwitch[] = "saw";
constexpr char kWhiteNoiseSwitch[] = "noise";
constexpr char kFrequencyDefaultHz[] = "400";

constexpr char kAmplitudeSwitch[] = "amp";
constexpr char kAmplitudeDefaultScale[] = "0.5";

constexpr char kDurationSwitch[] = "dur";
constexpr char kDurationDefaultSecs[] = "2";
constexpr char kMSecPerPayloadSwitch[] = "ms";
constexpr char kMSecPerPayloadDefault[] = "10";

constexpr char kSaveToFileSwitch[] = "wav";
constexpr char kSaveToFileDefaultName[] = "/tmp/signal_generator.wav";

constexpr char kRendererGainSwitch[] = "rgain";
constexpr char kRendererGainDefaultDb[] = "0.0";
constexpr char kSystemGainSwitch[] = "sgain";
constexpr char kSystemGainDefaultDb[] = "-12.0";

constexpr char kPlayToLastSwitch[] = "last";
constexpr char kPlayToAllSwitch[] = "all";

constexpr char kHelpSwitch[] = "help";
}  // namespace

void usage(const char* prog_name) {
  printf("\nUsage: %s [--option] [...]\n", prog_name);
  printf("Generate and play an audio signal to the preferred renderer.\n");
  printf("\nAdditional optional settings include:\n");

  printf("\t--%s=<NUM_CHANS>\tSpecify number of output channels (default %s)\n",
         kNumChannelsSwitch, kNumChannelsDefault);
  printf("\t--%s=<FRAME_RATE>\tSet output frame rate in Hertz (default %s)\n",
         kFrameRateSwitch, kFrameRateDefaultHz);
  printf("\t--%s, --i\t\tEmit signal as 16-bit integer (default float32)\n",
         kInt16FormatSwitch);

  printf(
      "\n\t--%s[=<FREQ>]  \tPlay sine of given frequency, in Hz (default %s)\n",
      kSineWaveSwitch, kFrequencyDefaultHz);
  printf("\t--%s[=<FREQ>]  \tPlay square wave (default %s Hz)\n",
         kSquareWaveSwitch, kFrequencyDefaultHz);
  printf("\t--%s[=<FREQ>]  \tPlay rising sawtooth wave (default %s Hz)\n",
         kSawtoothWaveSwitch, kFrequencyDefaultHz);
  printf("\t--%s  \t\tPlay pseudo-random 'white' noise\n", kWhiteNoiseSwitch);
  printf("\t\t\t\tIn the absence of --%s, --%s or --%s, a sine is played\n",
         kSquareWaveSwitch, kSawtoothWaveSwitch, kWhiteNoiseSwitch);

  printf(
      "\n\t--%s=<AMPL>\t\tSet signal amplitude (full-scale=1.0, default %s)\n",
      kAmplitudeSwitch, kAmplitudeDefaultScale);
  printf("\n\t--%s=<DURATION>\tSet playback length, in seconds (default %s)\n",
         kDurationSwitch, kDurationDefaultSecs);
  printf(
      "\t--%s=<MSEC>\t\tSet data buffer size, in milliseconds (default %s)\n",
      kMSecPerPayloadSwitch, kMSecPerPayloadDefault);

  printf("\n\t--%s[=<FILEPATH>]\tAlso save signal to .wav file (default %s)\n",
         kSaveToFileSwitch, kSaveToFileDefaultName);
  printf("\t\t\t\tNote: .wav file contents are unaffected by gain settings\n");

  printf(
      "\n\t--%s=<GAIN>\t\tSet Renderer gain to [%.1f, %.1f] dB (default %s)\n",
      kRendererGainSwitch, fuchsia::media::kMutedGain, fuchsia::media::kMaxGain,
      kRendererGainDefaultDb);
  printf("\t--%s=<GAIN>\t\tSet System gain to [%.1f, 0.0] dB (default %s)\n",
         kSystemGainSwitch, fuchsia::media::kMutedGain, kSystemGainDefaultDb);
  printf("\t\t\t\tNote: changes to System gain persist after playback.\n");

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

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto startup_context = fuchsia::sys::StartupContext::CreateFromStartupInfo();

  media::tools::MediaApp media_app(
      [&loop]() { async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); }); });

  std::string num_channels_str = command_line.GetOptionValueWithDefault(
      kNumChannelsSwitch, kNumChannelsDefault);
  media_app.set_num_channels(fxl::StringToNumber<uint32_t>(num_channels_str));

  std::string frame_rate_str = command_line.GetOptionValueWithDefault(
      kFrameRateSwitch, kFrameRateDefaultHz);
  media_app.set_frame_rate(fxl::StringToNumber<uint32_t>(frame_rate_str));

  if (command_line.HasOption("i") ||
      command_line.HasOption(kInt16FormatSwitch)) {
    media_app.set_int16_format(true);
  }

  if (command_line.HasOption(kWhiteNoiseSwitch)) {
    media_app.set_output_type(media::tools::kOutputTypeNoise);
  } else {
    std::string frequency_str;
    if (command_line.HasOption(kSquareWaveSwitch)) {
      media_app.set_output_type(media::tools::kOutputTypeSquare);
      command_line.GetOptionValue(kSquareWaveSwitch, &frequency_str);
    } else if (command_line.HasOption(kSawtoothWaveSwitch)) {
      media_app.set_output_type(media::tools::kOutputTypeSawtooth);
      command_line.GetOptionValue(kSawtoothWaveSwitch, &frequency_str);
    } else {
      media_app.set_output_type(media::tools::kOutputTypeSine);
      command_line.GetOptionValue(kSineWaveSwitch, &frequency_str);
    }
    if (frequency_str == "") {
      frequency_str = kFrequencyDefaultHz;
    }
    media_app.set_frequency(fxl::StringToNumber<uint32_t>(frequency_str));
  }

  std::string amplitude_str = command_line.GetOptionValueWithDefault(
      kAmplitudeSwitch, kAmplitudeDefaultScale);
  media_app.set_amplitude(std::stof(amplitude_str));

  std::string duration_str = command_line.GetOptionValueWithDefault(
      kDurationSwitch, kDurationDefaultSecs);
  media_app.set_duration(fxl::StringToNumber<uint32_t>(duration_str));

  std::string msec_per_payload_str = command_line.GetOptionValueWithDefault(
      kMSecPerPayloadSwitch, kMSecPerPayloadDefault);
  media_app.set_msec_per_payload(
      fxl::StringToNumber<uint32_t>(msec_per_payload_str));

  std::string renderer_gain_str = command_line.GetOptionValueWithDefault(
      kRendererGainSwitch, kRendererGainDefaultDb);
  media_app.set_renderer_gain(std::stof(renderer_gain_str));

  if (command_line.HasOption(kSystemGainSwitch)) {
    media_app.set_will_set_system_gain(
        command_line.HasOption(kSystemGainSwitch));
    std::string system_gain_str = command_line.GetOptionValueWithDefault(
        kSystemGainSwitch, kSystemGainDefaultDb);
    media_app.set_system_gain(std::stof(system_gain_str));
  }

  if (command_line.HasOption(kPlayToLastSwitch)) {
    if (command_line.HasOption(kPlayToAllSwitch)) {
      usage(argv[0]);
      return 0;
    }
    media_app.set_will_set_audio_policy(true);
    media_app.set_audio_policy(
        fuchsia::media::AudioOutputRoutingPolicy::kLastPluggedOutput);
  }
  if (command_line.HasOption(kPlayToAllSwitch)) {
    media_app.set_will_set_audio_policy(true);
    media_app.set_audio_policy(
        fuchsia::media::AudioOutputRoutingPolicy::kAllPluggedOutputs);
  }

  if (command_line.HasOption(kSaveToFileSwitch)) {
    media_app.set_save_to_file(command_line.HasOption(kSaveToFileSwitch));
    media_app.set_save_file_name(command_line.GetOptionValueWithDefault(
        kSaveToFileSwitch, kSaveToFileDefaultName));
  }

  media_app.Run(startup_context.get());

  // We've set everything going. Wait for our message loop to return.
  loop.Run();

  return 0;
}
