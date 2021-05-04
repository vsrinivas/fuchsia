// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/media/audio/tools/wav_player/wav_player.h"

namespace {

constexpr char kFlexibleClockSwitch[] = "flexible-clock";

constexpr char kRenderUsageSwitch[] = "usage";
constexpr char kRenderUsageDefault[] = "MEDIA";

constexpr char kRenderUsageGainSwitch[] = "usage-gain";
constexpr char kRenderUsageGainDefaultDb[] = "0.0";
constexpr char kRenderUsageVolumeSwitch[] = "usage-vol";
constexpr char kRenderUsageVolumeDefault[] = "1.0";

constexpr char kStreamGainSwitch[] = "gain";
constexpr char kStreamGainDefaultDb[] = "0.0";
constexpr char kStreamMuteSwitch[] = "mute";
constexpr char kStreamMuteDefault[] = "1";

constexpr char kFramesPerPacketSwitch[] = "frames";
constexpr char kFramesPerPacketDefault[] = "960";

constexpr char kFramesPerPayloadBufferSwitch[] = "buffer";
constexpr char kFramesPerPayloadBufferDefault[] = "96000";

constexpr char kUltrasoundSwitch[] = "ultrasound";

constexpr char kLoopSwitch[] = "loop";

constexpr char kVerboseSwitch[] = "v";

constexpr char kHelpSwitch[] = "help";
constexpr char kHelp2Switch[] = "?";

constexpr std::array<const char*, 6> kUltrasoundInvalidOptions = {
    kFlexibleClockSwitch, kStreamGainSwitch,      kStreamMuteSwitch,
    kRenderUsageSwitch,   kRenderUsageGainSwitch, kRenderUsageVolumeSwitch,
};

}  // namespace

void usage(const char* prog_name) {
  printf("\nUsage: %s [--option] [...] <AUDIO_FILE>\n", prog_name);
  printf("Play a WAV audio file to the preferred output device.\n");
  printf("\nValid options:\n");

  printf("\n    By default, do not set the local stream gain/mute (unity %.1f dB, unmuted)\n",
         kUnityGainDb);
  printf("  --%s[=<GAIN_DB>]\t Set stream gain, in dB (min %.1f, max %.1f, default %s)\n",
         kStreamGainSwitch, fuchsia::media::audio::MUTED_GAIN_DB,
         fuchsia::media::audio::MAX_GAIN_DB, kStreamGainDefaultDb);
  printf("  --%s[=<0|1>]\t Set stream mute (0=Unmute or 1=Mute; Mute if only '--%s' is provided)\n",
         kStreamMuteSwitch, kStreamMuteSwitch);

  printf("\n    By default, use a %s stream\n", kRenderUsageDefault);
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

  printf("\n    By default, do not change this RENDER_USAGE's volume or gain\n");
  printf(
      "  --%s[=<VOLUME>] Set render usage volume (min %.1f, max %.1f, %s if flag with no value)\n",
      kRenderUsageVolumeSwitch, fuchsia::media::audio::MIN_VOLUME,
      fuchsia::media::audio::MAX_VOLUME, kRenderUsageVolumeDefault);
  printf("  --%s[=<DB>]\t Set render usage gain, in dB (min %.1f, max %.1f, default %s)\n",
         kRenderUsageGainSwitch, fuchsia::media::audio::MUTED_GAIN_DB, kUnityGainDb,
         kRenderUsageGainDefaultDb);
  printf("    Changes to usage volume/gain are systemwide and persist after the utility exits.\n");

  printf("\n    By default, send packets of %s frames, in a payload buffer of %s frames\n",
         kFramesPerPacketDefault, kFramesPerPayloadBufferDefault);
  printf("  --%s=<FRAMES>\t Set packet size, in frames\n", kFramesPerPacketSwitch);
  printf(
      "  --%s=<FRAMES>\t Set payload buffer size, in frames (must exceed renderer "
      "MinLeadTime)\n",
      kFramesPerPayloadBufferSwitch);

  printf("\n    Use the default reference clock unless specified otherwise\n");
  printf(
      "  --%s\t Request and use the 'flexible' reference clock provided by the Audio "
      "service\n",
      kFlexibleClockSwitch);

  printf("\n  --%s\t\t Play through the ultrasound API", kUltrasoundSwitch);
  printf("\n\t\t\t\t(The file must match this device's 'native' ultrasonic format)\n");

  printf("\n  --%s\t\t Continue playing the file until stopped by ctrl-c\n", kLoopSwitch);

  printf("\n  --%s\t\t\t Display per-packet information\n", kVerboseSwitch);

  printf("  --%s, --%s\t\t Show this message\n\n", kHelpSwitch, kHelp2Switch);
}

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto& pos_args = command_line.positional_args();
  if (command_line.HasOption(kHelpSwitch) || command_line.HasOption(kHelp2Switch)) {
    usage(argv[0]);
    return 0;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  media::tools::WavPlayer::Options options;

  options.quit_callback = [&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  };

  if (pos_args.size() < 1) {
    fprintf(stderr, "No filename specified\n\n");
    return 1;
  }
  options.file_name = pos_args[0].c_str();
  options.loop_playback = command_line.HasOption(kLoopSwitch);
  options.verbose = command_line.HasOption(kVerboseSwitch);

  options.ultrasound = command_line.HasOption(kUltrasoundSwitch);

  if (options.ultrasound) {
    for (auto& invalid_option : kUltrasoundInvalidOptions) {
      if (command_line.HasOption(std::string(invalid_option))) {
        fprintf(stderr, "--ultrasound cannot be used with --%s\n", invalid_option);
        usage(argv[0]);
        return 1;
      }
    }
  }

  if (command_line.HasOption(kRenderUsageSwitch)) {
    std::string usage_option;
    command_line.GetOptionValue(kRenderUsageSwitch, &usage_option);
    auto it = std::find_if(kRenderUsageOptions.cbegin(), kRenderUsageOptions.cend(),
                           [&usage_option](auto usage_string_and_usage) {
                             return usage_option == usage_string_and_usage.first;
                           });
    if (it == kRenderUsageOptions.cend()) {
      fprintf(stderr, "Unrecognized AudioRenderUsage %s\n\n", usage_option.c_str());
      usage(argv[0]);
      return 1;
    }
    options.usage = it->second;
  }

  // Handle render usage volume and gain
  if (command_line.HasOption(kRenderUsageVolumeSwitch)) {
    std::string usage_volume_str;
    command_line.GetOptionValue(kRenderUsageVolumeSwitch, &usage_volume_str);
    if (usage_volume_str == "") {
      usage_volume_str = kRenderUsageVolumeDefault;
    }
    options.usage_volume = std::stof(usage_volume_str);
  }

  if (command_line.HasOption(kRenderUsageGainSwitch)) {
    std::string usage_gain_str;
    command_line.GetOptionValue(kRenderUsageGainSwitch, &usage_gain_str);
    if (usage_gain_str == "") {
      usage_gain_str = kRenderUsageGainDefaultDb;
    }
    options.usage_gain_db = std::stof(usage_gain_str);
  }

  // Handle stream-local gain/mute
  if (command_line.HasOption(kStreamGainSwitch)) {
    std::string stream_gain_str;
    command_line.GetOptionValue(kStreamGainSwitch, &stream_gain_str);
    if (stream_gain_str == "") {
      stream_gain_str = kStreamGainDefaultDb;
    }
    options.stream_gain_db = std::stof(stream_gain_str);
  }

  if (command_line.HasOption(kStreamMuteSwitch)) {
    std::string stream_mute_str;
    command_line.GetOptionValue(kStreamMuteSwitch, &stream_mute_str);
    if (stream_mute_str == "") {
      stream_mute_str = kStreamMuteDefault;
    }
    options.stream_mute = (fxl::StringToNumber<uint32_t>(stream_mute_str) != 0);
  }

  // Handle packet size
  std::string frames_per_packet_str =
      command_line.GetOptionValueWithDefault(kFramesPerPacketSwitch, kFramesPerPacketDefault);
  options.frames_per_packet = fxl::StringToNumber<uint32_t>(frames_per_packet_str);

  // Handle payload buffer size
  std::string frames_per_payload_str = command_line.GetOptionValueWithDefault(
      kFramesPerPayloadBufferSwitch, kFramesPerPayloadBufferDefault);
  options.frames_per_payload_buffer = fxl::StringToNumber<uint32_t>(frames_per_payload_str);

  // Handle any explicit reference clock selection.
  options.clock_type =
      (command_line.HasOption(kFlexibleClockSwitch) ? ClockType::Flexible : ClockType::Default);

  media::tools::WavPlayer wav_player(std::move(options));
  wav_player.Run(component_context.get());

  // We've set everything going. Wait for our message loop to return.
  loop.Run();

  return 0;
}
