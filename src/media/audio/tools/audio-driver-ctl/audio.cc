// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <thread>

#include <audio-proto-utils/format-utils.h>
#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <fbl/algorithm.h>

#include "generated-source.h"
#include "noise-source.h"
#include "sine-source.h"
#include "src/lib/fsl/tasks/fd_waiter.h"
#include "wav-sink.h"
#include "wav-source.h"

static constexpr float DEFAULT_PLUG_MONITOR_DURATION = 10.0f;
static constexpr float MIN_PLUG_MONITOR_DURATION = 0.5f;
static constexpr float MIN_PLAY_AMPLITUDE = 0.1f;
static constexpr float MAX_PLAY_AMPLITUDE = 1.0f;
static constexpr float DEFAULT_PLAY_DURATION = std::numeric_limits<float>::max();
static constexpr float DEFAULT_PLAY_AMPLITUDE = MAX_PLAY_AMPLITUDE;
static constexpr float MIN_PLAY_DURATION = 0.001f;
static constexpr float DEFAULT_TONE_FREQ = 440.0f;
static constexpr float MIN_TONE_FREQ = 15.0f;
static constexpr float MAX_TONE_FREQ = 96'000.0f;
static constexpr float DEFAULT_RECORD_DURATION = std::numeric_limits<float>::max();
static constexpr uint32_t DEFAULT_FRAME_RATE = 48000;
static constexpr uint32_t DEFAULT_BITS_PER_SAMPLE = 16;
static constexpr uint32_t DEFAULT_CHANNELS = 2;
static constexpr uint32_t DEFAULT_ACTIVE_CHANNELS = SineSource::kAllChannelsActive;
static constexpr audio_sample_format_t AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT =
    static_cast<audio_sample_format_t>(AUDIO_SAMPLE_FORMAT_8BIT |
                                       AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED);

enum class Command {
  INVALID,
  INFO,
  MUTE,
  UNMUTE,
  AGC,
  GAIN,
  PLUG_MONITOR,
  TONE,
  NOISE,
  PLAY,
  LOOP,
  RECORD,
  DUPLEX,
};

enum class Type : uint8_t { INPUT, OUTPUT, DUPLEX };

static std::optional<uint32_t> GetUint32(const char* arg) {
  char* end = nullptr;
  auto result = strtol(arg, &end, 0);
  if (*end != '\0' || result < 0 || (result == 0 && arg == end)) {
    return {};
  }
  return {result};
}

void usage(const char* prog_name) {
  // clang-format off
  printf("usage:\n");
  printf("%s [options] <cmd> <cmd params>\n", prog_name);
  printf("\nOptions\n");
  printf("  When options are specified, they must occur before the command and command\n"
         "  arguments.  Valid options include...\n"
         "  -d <device id>   : Dev node id for the audio device to use.  Defaults to 0.\n"
         "  -t <device type> : The type of device to open, either input or output.  Ignored if\n"
         "                     the command given implies the device type (play, record, duplex,\n"
         "                     etc). Otherwise, defaults to output.\n"
         "  -r <frame rate>  : Frame rate to use.  Defaults to 48000 Hz\n"
         "  -b <bits/sample> : Bits per sample to use.  Defaults to 16\n"
         "  -c <channels>    : Number of channels to use.  Defaults to 2\n"
         "  -a <active>      : Active channel mask (e.g. 0xf or 15 for channels 0, 1, 2 and 3).\n"
         "                     Defaults to all channels.\n");
  printf("\nValid command are\n");
  printf("info   : Fetches capability and status info for the specified stream\n");
  printf("mute   : Mute the specified stream\n");
  printf("unmute : Unmute the specified stream\n");
  printf("agc    : Params : (on|off)\n");
  printf("         Enable or disable AGC for the specified input stream.\n");
  printf("gain   : Params : <db_gain>\n");
  printf("         Set the gain of the stream to the specified level\n");
  printf("pmon   : Params : [<duration>]\n"
         "         Monitor the plug state of the specified stream for the\n"
         "         specified amount of time.  Duration defaults to %.1fs and is\n"
         "         floored at %u mSec\n",
         DEFAULT_PLUG_MONITOR_DURATION,
         static_cast<int>(MIN_PLUG_MONITOR_DURATION * 1000));
  printf("tone   : Params : [<freq>] [<duration>] [<amplitude>]\n"
         "         Play a sinusoidal tone of the specified frequency for the\n"
         "         specified duration.  Frequency is clamped on the range\n"
         "         [%.1f, %.1f] Hz.  Default is %.1f Hz.\n"
         "         Duration is given in seconds and floored at %d mSec.\n"
         "         If duration is unspecified plays until a key is pressed.\n"
         "         Output will be scaled by specified amplitude if provided.\n"
         "         Amplitude will be clamped between %.1f and %.1f\n",
          MIN_TONE_FREQ,
          MAX_TONE_FREQ,
          DEFAULT_TONE_FREQ,
          static_cast<int>(MIN_PLAY_DURATION * 1000),
          MIN_PLAY_AMPLITUDE,
          DEFAULT_PLAY_AMPLITUDE);
  printf("noise  : Params : [<duration>]\n"
         "         Play pseudo-white noise for the specified duration.  Duration is\n"
         "         given in seconds and floored at %d mSec.\n"
         "         If duration is unspecified plays until a key is pressed.\n",
          static_cast<int>(MIN_PLAY_DURATION * 1000));
  printf("play   : Params : <file>\n");
  printf("         Play the specified WAV file on the selected output.\n");
  printf("loop   : Params : <file>\n");
  printf("         Repeat the specified WAV file on the selected output until a key is pressed\n");
  printf("record : Params : <file> [<duration>]\n"
         "         Record to the specified WAV file from the selected input.\n"
         "         Duration is specified in seconds.\n"
         "         If duration is unspecified records until a key is pressed.\n");
  printf("duplex : Params : <play-file> <record-file>\n"
         "         Play play-file on the selected output and record record-file from\n"
         "         the selected input.\n");
  // clang-format on
}

void dump_formats(const audio::utils::AudioDeviceStream& stream) {
  stream.GetSupportedFormats([](const fuchsia_hardware_audio::wire::SupportedFormats& formats) {
    auto& pcm = formats.pcm_supported_formats();
    printf("\nNumber of channels      :");
    bool has_attributes = false;
    for (auto i : pcm.channel_sets()) {
      printf(" %zu", i.attributes().count());
      for (auto j : i.attributes()) {
        if (j.has_min_frequency()) {
          has_attributes = true;
        }
        if (j.has_max_frequency()) {
          has_attributes = true;
        }
      }
    }
    if (has_attributes) {
      printf("\nChannels attributes     :");
      for (auto i : pcm.channel_sets()) {
        for (auto j : i.attributes()) {
          printf(" ");
          if (j.has_min_frequency()) {
            printf("%u", j.min_frequency());
          }
          printf("/");
          if (j.has_max_frequency()) {
            printf("%u", j.max_frequency());
          }
        }
        printf(" (min/max Hz for %zu channels)", i.attributes().count());
      }
    }
    printf("\nFrame rate              :");
    for (auto i : pcm.frame_rates()) {
      printf(" %uHz", i);
    }
    printf("\nBits per channel        :");
    for (auto i : pcm.bytes_per_sample()) {
      printf(" %u", 8 * i);
    }
    printf("\nValid bits per channel  :");
    for (auto i : pcm.valid_bits_per_sample()) {
      printf(" %u", i);
    }
    printf("\n");
  });
}

static void FixupStringRequest(audio_stream_cmd_get_string_resp_t* resp, zx_status_t res) {
  if (res != ZX_OK) {
    snprintf(reinterpret_cast<char*>(resp->str), sizeof(resp->str), "<err %d>", res);
    return;
  }

  if (resp->strlen > sizeof(resp->str)) {
    snprintf(reinterpret_cast<char*>(resp->str), sizeof(resp->str), "<bad strllen %u>",
             resp->strlen);
    return;
  }

  // We are going to display this string using ASCII, but it is encoded using
  // UTF8.  Go over the string and replace unprintable characters with
  // something else.  Also replace embedded nulls with a space.  Finally,
  // ensure that the string is null terminated.
  uint32_t len = std::min<uint32_t>(sizeof(resp->str) - 1, resp->strlen);
  uint32_t i;
  for (i = 0; i < len; ++i) {
    if (resp->str[i] == 0) {
      resp->str[i] = ' ';
    } else if (!isprint(resp->str[i])) {
      resp->str[i] = '?';
    }
  }

  resp->str[i] = 0;
}

int Play(std::unique_ptr<audio::utils::AudioDeviceStream> stream, const char* play_wav_filename,
         uint32_t active, const audio::utils::Duration& duration_config) {
  WAVSource wav_source;
  auto res = wav_source.Initialize(play_wav_filename, active, duration_config);
  if (res != ZX_OK)
    return res;

  return static_cast<audio::utils::AudioOutput*>(stream.get())->Play(wav_source);
}

int Record(std::unique_ptr<audio::utils::AudioDeviceStream> stream, const char* record_wav_filename,
           uint32_t frame_rate, uint32_t channels, uint32_t active,
           audio_sample_format_t sample_format, const audio::utils::Duration& duration_config) {
  auto res = stream->SetFormat(frame_rate, static_cast<uint16_t>(channels), active, sample_format);
  if (res != ZX_OK) {
    printf("Failed to set format (rate %u, chan %u, fmt 0x%08x, res %d)\n", frame_rate, channels,
           sample_format, res);
    return -1;
  }

  WAVSink wav_sink;
  res = wav_sink.Initialize(record_wav_filename);
  if (res != ZX_OK)
    return res;

  return static_cast<audio::utils::AudioInput*>(stream.get())->Record(wav_sink, duration_config);
}

int Duplex(std::unique_ptr<audio::utils::AudioDeviceStream> play_stream,
           std::unique_ptr<audio::utils::AudioDeviceStream> record_stream,
           const char* play_wav_filename, const char* record_wav_filename, uint32_t frame_rate,
           uint32_t channels, uint32_t active, audio_sample_format_t sample_format) {
  // Initialize recording.
  auto res =
      record_stream->SetFormat(frame_rate, static_cast<uint16_t>(channels), active, sample_format);
  if (res != ZX_OK) {
    printf("Failed to set format (rate %u, chan %u, fmt 0x%08x, res %d)\n", frame_rate, channels,
           sample_format, res);
    return -1;
  }

  WAVSink wav_sink;
  res = wav_sink.Initialize(record_wav_filename);
  if (res != ZX_OK)
    return res;

  auto input = static_cast<audio::utils::AudioInput*>(record_stream.get());
  res = input->RecordPrepare(wav_sink);
  if (res != ZX_OK)
    return res;

  // Initialize playback.
  WAVSource wav_source;
  // duration not in loop mode, unused.
  float unused_duration = 0.f;
  res = wav_source.Initialize(play_wav_filename, active, unused_duration);
  if (res != ZX_OK)
    return res;

  auto output = static_cast<audio::utils::AudioOutput*>(play_stream.get());
  res = output->PlayPrepare(wav_source);
  if (res != ZX_OK)
    return res;

  // Start recording and playback.
  res = input->StartRingBuffer();
  auto res2 = output->StartRingBuffer();

  if (res != ZX_OK) {
    printf("Failed to start capture (res %d)\n", res);
    return res;
  }
  if (res2 != ZX_OK) {
    printf("Failed to start playback (res %d)\n", res);
    return res;
  }
  int64_t record_start = input->start_time();
  int64_t playback_start = output->start_time();

  // Complete recording and playback.
  zx_status_t play_completion_error = ZX_ERR_INTERNAL;
  std::atomic<bool> play_done = {};
  auto th = std::thread([&]() {
    play_completion_error = output->PlayToCompletion(wav_source);
    play_done.store(true);
  });
  res = input->RecordToCompletion(wav_sink, [&]() -> bool { return !play_done.load(); });
  if (res != ZX_OK) {
    printf("Failed to complete recording (res %d)\n", res);
    th.join();
    return res;
  }
  th.join();
  if (play_completion_error != ZX_OK) {
    printf("Failed to complete playback (res %d)\n", play_completion_error);
    return play_completion_error;
  }

  // Now report known delays.
  printf(
      "Duplex delays:\n"
      "  Play start      : %ld usecs\n"
      "  Input external  : %ld usecs\n"
      "  Output external : %ld usecs\n"
      "  Total           : %ld usecs\n",
      (playback_start - record_start) / 1000, input->external_delay_nsec() / 1000,
      output->external_delay_nsec() / 1000,
      (playback_start - record_start + input->external_delay_nsec() +
       output->external_delay_nsec()) /
          1000);

  return res;
}
zx_status_t dump_stream_info(const audio::utils::AudioDeviceStream& stream) {
  zx_status_t res;
  printf("Info for audio %s at \"%s\"\n", stream.input() ? "input" : "output", stream.name());

  // Grab and display some of the interesting properties of the device,
  // including its unique ID, its manufacturer name, and its product name.
  audio_stream_cmd_get_unique_id_resp_t uid_resp;
  res = stream.GetUniqueId(&uid_resp);
  if (res != ZX_OK) {
    printf("Failed to fetch unique ID! (res %d)\n", res);
    return res;
  }

  const auto& uid = uid_resp.unique_id.data;
  static_assert(sizeof(uid) == 16, "Unique ID is not 16 bytes long!\n");
  printf("  Unique ID    : %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
         uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7], uid[8], uid[9], uid[10],
         uid[11], uid[12], uid[13], uid[14], uid[15]);

  audio_stream_cmd_get_string_resp_t str_resp;
  res = stream.GetString(AUDIO_STREAM_STR_ID_MANUFACTURER, &str_resp);
  FixupStringRequest(&str_resp, res);
  printf("  Manufacturer : %s\n", str_resp.str);

  res = stream.GetString(AUDIO_STREAM_STR_ID_PRODUCT, &str_resp);
  FixupStringRequest(&str_resp, res);
  printf("  Product      : %s\n", str_resp.str);

  // Fetch and print the current gain settings for this audio stream.
  // Since we reconnect to the audio stream every time we run this uapp and we are guaranteed by the
  // audio driver interface definition that the driver will reply to the first watch request, we
  // can get the gain state by issuing a watch FIDL call.
  audio_stream_cmd_get_gain_resp gain_state;
  res = stream.WatchGain(&gain_state);
  if (res != ZX_OK) {
    printf("Failed to fetch gain information! (res %d)\n", res);
    return res;
  }

  printf("  Current Gain : %.2f dB (%smuted%s)\n", gain_state.cur_gain,
         gain_state.cur_mute ? "" : "un",
         gain_state.can_agc ? (gain_state.cur_agc ? ", AGC on" : ", AGC off") : "");
  printf("  Gain Caps    : ");
  if ((gain_state.min_gain == gain_state.max_gain) && (gain_state.min_gain == 0.0f)) {
    printf("fixed 0 dB gain");
  } else if (gain_state.gain_step == 0.0f) {
    printf("gain range [%.2f, %.2f] dB (continuous)", gain_state.min_gain, gain_state.max_gain);
  } else {
    printf("gain range [%.2f, %.2f] in %.2f dB steps", gain_state.min_gain, gain_state.max_gain,
           gain_state.gain_step);
  }
  printf("; %s mute", gain_state.can_mute ? "can" : "cannot");
  printf("; %s AGC\n", gain_state.can_agc ? "can" : "cannot");

  // Fetch and print the current pluged/unplugged state for this audio stream.
  // Since we reconnect to the audio stream every time we run this uapp and we are guaranteed by the
  // audio driver interface definition that the driver will reply to the first watch request, we
  // can get the plug state by issuing a watch FIDL call.
  audio_stream_cmd_plug_detect_resp plug_state;
  res = stream.WatchPlugState(&plug_state);
  if (res != ZX_OK) {
    printf("Failed to fetch plug state information! (res %d)\n", res);
    return res;
  }

  printf("  Plug State   : %splugged\n", plug_state.flags & AUDIO_PDNF_PLUGGED ? "" : "un");
  printf("  Plug Time    : %lu\n", plug_state.plug_state_time);
  printf("  PD Caps      : %s\n",
         (plug_state.flags & AUDIO_PDNF_HARDWIRED)
             ? "hardwired"
             : ((plug_state.flags & AUDIO_PDNF_CAN_NOTIFY) ? "dynamic (async)"
                                                           : "dynamic (synchronous)"));

  // Fetch and print the currently supported audio formats for this audio stream.
  dump_formats(stream);

  return ZX_OK;
}

int main(int argc, const char** argv) {
  Type type = Type::OUTPUT;
  uint32_t dev_id = 0;
  uint32_t frame_rate = DEFAULT_FRAME_RATE;
  uint32_t bits_per_sample = DEFAULT_BITS_PER_SAMPLE;
  uint32_t channels = DEFAULT_CHANNELS;
  uint32_t active = DEFAULT_ACTIVE_CHANNELS;
  Command cmd = Command::INVALID;
  auto print_usage = fit::defer([prog_name = argv[0]]() { usage(prog_name); });
  int arg = 1;

  if (arg >= argc)
    return -1;

  struct {
    const char* name;
    const char* tag;
    uint32_t* val;
  } OPTIONS[] = {
      // clang-format off
    { .name = "-d", .tag = "device ID",   .val = &dev_id },
    { .name = "-r", .tag = "frame rate",  .val = &frame_rate },
    { .name = "-b", .tag = "bits/sample", .val = &bits_per_sample },
    { .name = "-c", .tag = "channels",    .val = &channels },
    { .name = "-a", .tag = "active",      .val = &active },
      // clang-format on
  };

  static const struct {
    const char* name;
    Command cmd;
    bool force_out;
    bool force_in;
  } COMMANDS[] = {
      // clang-format off
    { "info",   Command::INFO,          false, false },
    { "mute",   Command::MUTE,          false, false },
    { "unmute", Command::UNMUTE,        false, false },
    { "agc",    Command::AGC,           false, true  },
    { "gain",   Command::GAIN,          false, false },
    { "pmon",   Command::PLUG_MONITOR,  false, false },
    { "tone",   Command::TONE,          true,  false },
    { "noise",  Command::NOISE,         true,  false },
    { "play",   Command::PLAY,          true,  false },
    { "loop",   Command::LOOP,          true,  false },
    { "record", Command::RECORD,        false, true  },
    { "duplex", Command::DUPLEX,        false, false },
      // clang-format on
  };

  while (arg < argc) {
    // Check to see if this is an integer option
    bool parsed_option = false;
    for (const auto& o : OPTIONS) {
      if (!strcmp(o.name, argv[arg])) {
        // Looks like this is an integer argument we care about.
        // Attempt to parse it.
        if (++arg >= argc)
          return -1;
        std::optional<uint32_t> value = GetUint32(argv[arg]);
        if (!value.has_value()) {
          printf("Failed to parse %s option, \"%s\"\n", o.tag, argv[arg]);
          return -1;
        }
        *o.val = value.value();
        ++arg;
        parsed_option = true;
        break;
      }
    }

    // If we successfully parse an integer option, continue on to the next
    // argument (if any).
    if (parsed_option)
      continue;

    // Was this the device type flag?
    if (!strcmp("-t", argv[arg])) {
      if (++arg >= argc)
        return -1;
      if (!strcmp("input", argv[arg])) {
        type = Type::INPUT;
      } else if (!strcmp("output", argv[arg])) {
        type = Type::OUTPUT;
      } else {
        printf("Invalid input/output specifier \"%s\".\n", argv[arg]);
        return -1;
      }
      ++arg;
      continue;
    }

    // Well, this didn't look like an option we understand, so it must be a
    // command.  Attempt to figure out what command it was.
    for (const auto& entry : COMMANDS) {
      if (!strcmp(entry.name, argv[arg])) {
        cmd = entry.cmd;
        parsed_option = true;
        arg++;

        if (entry.force_out)
          type = Type::OUTPUT;
        if (entry.force_in)
          type = Type::INPUT;

        break;
      }
    }

    if (!parsed_option) {
      printf("Failed to parse command ID \"%s\"\n", argv[arg]);
      return -1;
    }

    break;
  }

  if (cmd == Command::INVALID) {
    printf("Failed to find valid command ID.\n");
    return -1;
  }

  audio_sample_format_t sample_format;
  switch (bits_per_sample) {
    case 8:
      sample_format = AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT;
      break;
    case 16:
      sample_format = AUDIO_SAMPLE_FORMAT_16BIT;
      break;
    case 20:
      sample_format = AUDIO_SAMPLE_FORMAT_20BIT_IN32;
      break;
    case 24:
      sample_format = AUDIO_SAMPLE_FORMAT_24BIT_IN32;
      break;
    case 32:
      sample_format = AUDIO_SAMPLE_FORMAT_32BIT;
      break;
    default:
      printf("Unsupported number of bits per sample (%u)\n", bits_per_sample);
      return -1;
  }

  float tone_freq = 440.0;
  float duration;
  float amplitude = DEFAULT_PLAY_AMPLITUDE;
  const char* play_wav_filename = nullptr;
  const char* record_wav_filename = nullptr;
  float target_gain = -100.0;
  bool enb_agc = false;

  // Parse any additional arguments
  switch (cmd) {
    case Command::GAIN:
      if (arg >= argc)
        return -1;
      if (sscanf(argv[arg], "%f", &target_gain) != 1) {
        printf("Failed to parse gain \"%s\"\n", argv[arg]);
        return -1;
      }
      arg++;
      break;

    case Command::AGC:
      if (arg >= argc)
        return -1;
      if (strcasecmp(argv[arg], "on") == 0) {
        enb_agc = true;
      } else if (strcasecmp(argv[arg], "off") == 0) {
        enb_agc = false;
      } else {
        printf("Failed to parse agc setting \"%s\"\n", argv[arg]);
        return -1;
      }
      arg++;
      break;

    case Command::PLUG_MONITOR:
      duration = DEFAULT_PLUG_MONITOR_DURATION;
      if (arg < argc) {
        if (sscanf(argv[arg], "%f", &duration) != 1) {
          printf("Failed to parse plug monitor duration \"%s\"\n", argv[arg]);
          return -1;
        }
        arg++;
        duration = std::max(duration, MIN_PLUG_MONITOR_DURATION);
      }
      break;

    case Command::TONE:
    case Command::NOISE:
      duration = DEFAULT_PLAY_DURATION;
      if (arg < argc) {
        if (cmd == Command::TONE) {
          if (sscanf(argv[arg], "%f", &tone_freq) != 1) {
            printf("Failed to parse tone frequency \"%s\"\n", argv[arg]);
            return -1;
          }
          arg++;
          tone_freq = std::clamp(tone_freq, MIN_TONE_FREQ, MAX_TONE_FREQ);
        }
        if (arg < argc) {
          if (sscanf(argv[arg], "%f", &duration) != 1) {
            printf("Failed to parse playback duration \"%s\"\n", argv[arg]);
            return -1;
          }
          arg++;
        }
        if (arg < argc) {
          if (sscanf(argv[arg], "%f", &amplitude) != 1) {
            printf("Failed to parse playback amplitude \"%s\"\n", argv[arg]);
            return -1;
          }
          arg++;
        }
        duration = std::max(duration, MIN_PLAY_DURATION);
        amplitude = std::min(amplitude, MAX_PLAY_AMPLITUDE);
        amplitude = std::max(amplitude, MIN_PLAY_AMPLITUDE);
      }
      break;

    case Command::PLAY:
      if (arg >= argc)
        return -1;
      play_wav_filename = argv[arg];
      arg++;

      break;

    case Command::RECORD:
      if (arg >= argc)
        return -1;
      record_wav_filename = argv[arg];
      arg++;

      duration = DEFAULT_RECORD_DURATION;
      if (arg < argc) {
        if (sscanf(argv[arg], "%f", &duration) != 1) {
          printf("Failed to parse record duration \"%s\"\n", argv[arg]);
          return -1;
        }
        arg++;
      }

      break;

    case Command::LOOP:
      if (arg >= argc)
        return -1;
      play_wav_filename = argv[arg];
      arg++;

      break;

    case Command::DUPLEX:
      if (arg >= argc)
        return -1;
      play_wav_filename = argv[arg];
      arg++;
      if (arg >= argc)
        return -1;
      record_wav_filename = argv[arg];
      arg++;
      type = Type::DUPLEX;
      break;

    default:
      break;
  }

  if (arg != argc) {
    printf("Invalid number of arguments.\n");
    return -1;
  }

  // Argument parsing is done, we can cancel the usage dump.
  print_usage.cancel();

  // Open the selected stream.
  std::unique_ptr<audio::utils::AudioDeviceStream> stream;
  std::unique_ptr<audio::utils::AudioDeviceStream> stream_duplex_record;
  switch (type) {
    case Type::INPUT:
      stream = audio::utils::AudioInput::Create(dev_id);
      break;
    case Type::OUTPUT:
      stream = audio::utils::AudioOutput::Create(dev_id);
      break;
    case Type::DUPLEX: {
      stream_duplex_record = audio::utils::AudioInput::Create(dev_id);
      if (stream_duplex_record == nullptr) {
        printf("Out of memory!\n");
        return ZX_ERR_NO_MEMORY;
      }
      // No need to log in the case of failure.  Open has already done so.
      zx_status_t res = stream_duplex_record->Open();
      if (res != ZX_OK) {
        return res;
      }
      stream = audio::utils::AudioOutput::Create(dev_id);
    } break;
  }
  if (stream == nullptr) {
    printf("Out of memory!\n");
    return ZX_ERR_NO_MEMORY;
  }

  // No need to log in the case of failure.  Open has already done so.
  zx_status_t res = stream->Open();
  if (res != ZX_OK)
    return res;

  async::Loop async_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_loop.StartThread("audio CLI wait for key");
  std::atomic<bool> pressed(false);
  fsl::FDWaiter fd_waiter(async_loop.dispatcher());

  auto cleanup = fit::defer([&async_loop] { async_loop.Shutdown(); });
  fd_waiter.Wait([&pressed](zx_status_t, uint32_t) { pressed.store(true); }, 0, POLLIN);
  auto loop_done = [&pressed]() -> bool { return !pressed.load(); };

  audio::utils::Duration duration_config = {};
  const bool interactive = duration == std::numeric_limits<float>::max();
  if (interactive) {
    duration_config = loop_done;
  } else {
    duration_config = duration;
  }

  // Execute the chosen command.
  switch (cmd) {
    case Command::INFO:
      return dump_stream_info(*stream);
    case Command::MUTE:
      return stream->SetMute(true);
    case Command::UNMUTE:
      return stream->SetMute(false);
    case Command::GAIN:
      return stream->SetGain(target_gain);
    case Command::AGC:
      return stream->SetAgc(enb_agc);
    case Command::PLUG_MONITOR:
      return stream->PlugMonitor(duration, nullptr);

    case Command::TONE: {
      if (stream->input()) {
        printf("The \"tone\" command can only be used on output streams.\n");
        return -1;
      }

      SineSource sine_source;
      res = sine_source.Init(tone_freq, amplitude, duration_config, frame_rate, channels, active,
                             sample_format);
      if (res != ZX_OK) {
        printf("Failed to initialize sine wav generator (res %d)\n", res);
        return res;
      }
      if (interactive) {
        printf("Playing %.2f Hz tone at %.2f amplitude until a key is pressed\n", tone_freq,
               amplitude);
      } else {
        printf("Playing %.2f Hz tone for %.2f seconds at %.2f amplitude\n", tone_freq,
               std::get<float>(duration_config), amplitude);
      }
      return static_cast<audio::utils::AudioOutput*>(stream.get())->Play(sine_source);
    }

    case Command::NOISE: {
      if (stream->input()) {
        printf("The \"noise\" command can only be used on output streams.\n");
        return -1;
      }

      NoiseSource noise_source;
      res = noise_source.Init(tone_freq, 1.0, duration_config, frame_rate, channels, active,
                              sample_format);
      if (res != ZX_OK) {
        printf("Failed to initialize white noise generator (res %d)\n", res);
        return res;
      }
      if (interactive) {
        printf("Playing white noise until a key is pressed\n");
      } else {
        printf("Playing white noise for %.2f seconds\n", std::get<float>(duration_config));
      }
      return static_cast<audio::utils::AudioOutput*>(stream.get())->Play(noise_source);
    }

    case Command::PLAY:
      if (stream->input()) {
        printf("The \"play\" command can only be used on output streams.\n");
        return -1;
      }
      return Play(std::move(stream), play_wav_filename, active, duration_config);

    case Command::LOOP:
      if (stream->input()) {
        printf("The \"loop\" command can only be used on output streams.\n");
        return -1;
      }
      duration_config = loop_done;
      printf("Looping %s until a key is pressed\n", play_wav_filename);
      return Play(std::move(stream), play_wav_filename, active, duration_config);

    case Command::RECORD:
      if (!stream->input()) {
        printf("The \"record\" command can only be used on input streams.\n");
        return -1;
      }
      if (interactive) {
        printf("Recording until a key is pressed\n");
      }
      return Record(std::move(stream), record_wav_filename, frame_rate, channels, active,
                    sample_format, duration_config);

    case Command::DUPLEX: {
      if (stream->input() || !stream_duplex_record || !stream_duplex_record->input()) {
        printf("The \"duplex\" command can only be used on one output and one input stream.\n");
        return -1;
      }

      return Duplex(std::move(stream), std::move(stream_duplex_record), play_wav_filename,
                    record_wav_filename, frame_rate, channels, active, sample_format);
    }

    default:
      ZX_DEBUG_ASSERT(false);
      return -1;
  }
}
