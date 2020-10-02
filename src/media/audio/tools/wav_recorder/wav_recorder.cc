// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/tools/wav_recorder/wav_recorder.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <poll.h>

#include <iostream>

#include "lib/media/audio/cpp/types.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"

namespace media::tools {

constexpr char kLoopbackOption[] = "loopback";
constexpr char kChannelsOption[] = "chans";
constexpr char kFrameRateOption[] = "rate";
constexpr char k24In32FormatOption[] = "int24";
constexpr char kPacked24FormatOption[] = "packed24";
constexpr char kInt16FormatOption[] = "int16";
constexpr char kGainOption[] = "gain";
constexpr char kMuteOption[] = "mute";
constexpr char kAsyncModeOption[] = "async";
constexpr char kFlexibleClockOption[] = "flexible-clock";
constexpr char kMonotonicClockOption[] = "monotonic-clock";
constexpr char kCustomClockOption[] = "custom-clock";
constexpr char kClockRateAdjustOption[] = "rate-adjust";
constexpr char kClockRateAdjustDefault[] = "-75";
constexpr char kPacketDurationOption[] = "packet-ms";
constexpr char kFileDurationOption[] = "duration";
constexpr char kUltrasoundOption[] = "ultrasound";
constexpr char kVerboseOption[] = "v";
constexpr char kShowUsageOption1[] = "help";
constexpr char kShowUsageOption2[] = "?";

constexpr std::array<const char*, 12> kUltrasoundInvalidOptions = {
    kLoopbackOption,       kChannelsOption,       kFrameRateOption,   k24In32FormatOption,
    kPacked24FormatOption, kInt16FormatOption,    kGainOption,        kMuteOption,
    kFlexibleClockOption,  kMonotonicClockOption, kCustomClockOption, kClockRateAdjustOption,
};

constexpr uint32_t kPayloadBufferId = 0;

WavRecorder::~WavRecorder() {
  if (payload_buf_virt_ != nullptr) {
    zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(payload_buf_virt_), payload_buf_size_);
  }
}

void WavRecorder::Run(sys::ComponentContext* app_context) {
  auto cleanup = fit::defer([this]() { Shutdown(); });
  const auto& pos_args = cmd_line_.positional_args();

  // Parse our args.
  if (cmd_line_.HasOption(kShowUsageOption1) || cmd_line_.HasOption(kShowUsageOption2)) {
    Usage();
    return;
  }

  verbose_ = cmd_line_.HasOption(kVerboseOption);
  loopback_ = cmd_line_.HasOption(kLoopbackOption);
  ultrasound_ = cmd_line_.HasOption(kUltrasoundOption);
  if (ultrasound_) {
    for (auto& invalid_option : kUltrasoundInvalidOptions) {
      if (cmd_line_.HasOption(std::string(invalid_option))) {
        fprintf(stderr, "--ultrasound cannot be used with --%s\n", invalid_option);
        Usage();
        exit(1);
      }
    }
  } else {
    // If user erroneously specifies float AND 24-in-32, prefer float.
    if (cmd_line_.HasOption(kPacked24FormatOption)) {
      pack_24bit_samples_ = true;
      sample_format_ = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
    } else if (cmd_line_.HasOption(k24In32FormatOption)) {
      sample_format_ = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
    } else if (cmd_line_.HasOption(kInt16FormatOption)) {
      sample_format_ = fuchsia::media::AudioSampleFormat::SIGNED_16;
    } else {
      sample_format_ = fuchsia::media::AudioSampleFormat::FLOAT;
    }
  }

  std::string opt;
  if (cmd_line_.GetOptionValue(kFileDurationOption, &opt)) {
    file_duration_specifed_ = true;

    float duration;
    if (opt == "") {
      duration = kDefaultFileDurationSecs;
    } else {
      CLI_CHECK(sscanf(opt.c_str(), "%f", &duration) == 1, "Duration must be numeric");
      CLI_CHECK(duration >= 0, "Duration cannot be negative");
      CLI_CHECK(duration <= kMaxFileDurationSecs, "Maximum duration is " << kMaxFileDurationSecs);
    }

    printf("\nWe will record for %.3f seconds.", duration);
    file_duration_ = zx::duration(duration * ZX_SEC(1));
  }

  // Handle any explicit reference clock selection. We allow Monotonic to be rate-adjusted,
  // otherwise rate-adjustment implies a custom clock which starts at value zero.
  if (cmd_line_.HasOption(kMonotonicClockOption)) {
    clock_type_ = ClockType::Monotonic;
  } else if (cmd_line_.HasOption(kCustomClockOption) ||
             cmd_line_.HasOption(kClockRateAdjustOption)) {
    clock_type_ = ClockType::Custom;
  } else if (cmd_line_.HasOption(kFlexibleClockOption)) {
    clock_type_ = ClockType::Flexible;
  } else {
    clock_type_ = ClockType::Default;
  }
  if (cmd_line_.HasOption(kClockRateAdjustOption)) {
    adjusting_clock_rate_ = true;
    std::string rate_adjust_str;
    if (cmd_line_.GetOptionValue(kClockRateAdjustOption, &rate_adjust_str)) {
      if (rate_adjust_str == "") {
        rate_adjust_str = kClockRateAdjustDefault;
      }
      CLI_CHECK(sscanf(rate_adjust_str.c_str(), "%i", &clock_rate_adjustment_) == 1,
                "Clock rate adjustment must be an integer");
      CLI_CHECK(clock_rate_adjustment_ >= ZX_CLOCK_UPDATE_MIN_RATE_ADJUST &&
                    clock_rate_adjustment_ <= ZX_CLOCK_UPDATE_MAX_RATE_ADJUST,
                "Clock rate adjustment must be between " << ZX_CLOCK_UPDATE_MIN_RATE_ADJUST
                                                         << " and "
                                                         << ZX_CLOCK_UPDATE_MAX_RATE_ADJUST);
    }
  }

  CLI_CHECK(pos_args.size() >= 1, "No filename specified");
  filename_ = pos_args[0].c_str();

  if (ultrasound_) {
    ultrasound_factory_ = app_context->svc()->Connect<fuchsia::ultrasound::Factory>();
    ultrasound_factory_->CreateCapturer(
        audio_capturer_.NewRequest(), [this](auto reference_clock, auto stream_type) {
          sample_format_ = stream_type.sample_format;
          channel_count_ = stream_type.channels;
          frames_per_second_ = stream_type.frames_per_second;

          ReceiveClockAndContinue(std::move(reference_clock), {stream_type});
          ultrasound_factory_.Unbind();
        });
  } else {
    // Connect to the audio service and obtain AudioCapturer and Gain interfaces.
    fuchsia::media::AudioPtr audio = app_context->svc()->Connect<fuchsia::media::Audio>();

    audio->CreateAudioCapturer(audio_capturer_.NewRequest(), loopback_);
    audio_capturer_->BindGainControl(gain_control_.NewRequest());
    gain_control_.set_error_handler([this](zx_status_t status) {
      CLI_CHECK(Shutdown(), "Client connection to fuchsia.media.GainControl failed");
    });

    EstablishReferenceClock();
  }

  audio_capturer_.set_error_handler([this](zx_status_t status) {
    CLI_CHECK(Shutdown(), "Client connection to fuchsia.media.AudioCapturer failed");
  });

  // Quit if someone hits a key.
  keystroke_waiter_.Wait([this](zx_status_t, uint32_t) { OnQuit(); }, STDIN_FILENO, POLLIN);

  cleanup.cancel();
}

void WavRecorder::Usage() {
  printf("\nUsage: %s [options] <filename>\n", cmd_line_.argv0().c_str());
  printf("Record an audio signal from the specified source to a .wav file.\n");
  printf("\nValid options:\n");

  printf("\n    By default, use the preferred input device\n");
  printf("  --%s\t\tCapture final-mix output from the preferred output device\n", kLoopbackOption);

  printf(
      "\n    By default, use device-preferred channel count and frame rate, in 32-bit float "
      "samples\n");
  printf("  --%s=<NUM_CHANS>\tSpecify the number of channels (min %u, max %u)\n", kChannelsOption,
         fuchsia::media::MIN_PCM_CHANNEL_COUNT, fuchsia::media::MAX_PCM_CHANNEL_COUNT);
  printf("  --%s=<rate>\t\tSpecify the capture frame rate, in Hz (min %u, max %u)\n",
         kFrameRateOption, fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
         fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
  printf("  --%s\t\tRecord and save as left-justified 24-in-32 int ('padded-24')\n",
         k24In32FormatOption);
  printf("  --%s\t\tRecord as 24-in-32 'padded-24'; save as 'packed-24'\n", kPacked24FormatOption);
  printf("  --%s\t\tRecord and save as 16-bit integer\n", kInt16FormatOption);

  printf("\n    By default, don't set AudioCapturer gain and mute (unity 0 dB and unmuted)\n");
  printf("  --%s[=<GAIN_DB>]\tSet stream gain, in dB (min %.1f, max +%.1f, default %.1f)\n",
         kGainOption, fuchsia::media::audio::MUTED_GAIN_DB, fuchsia::media::audio::MAX_GAIN_DB,
         kDefaultCaptureGainDb);
  printf("  --%s[=<0|1>]\tSet stream mute (0=Unmute or 1=Mute; Mute if only '--%s' is provided)\n",
         kMuteOption, kMuteOption);

  printf("\n    By default, use packet-by-packet ('synchronous') mode\n");
  printf("  --%s\t\tCapture using sequential-buffer ('asynchronous') mode\n", kAsyncModeOption);

  printf("\n    Use the default reference clock unless specified otherwise\n");
  printf("  --%s\tUse the 'flexible' reference clock provided by the Audio service\n",
         kFlexibleClockOption);
  printf("  --%s\tSet the local system monotonic clock as reference for this stream\n",
         kMonotonicClockOption);
  printf("  --%s\tUse a custom clock as this stream's reference clock\n", kCustomClockOption);
  printf("  --%s[=<PPM>]\tRun faster/slower than local system clock, in parts-per-million\n",
         kClockRateAdjustOption);
  printf("\t\t\t(min %d, max %d; %s if unspecified). Implies '--%s' if '--%s' is not specified\n",
         ZX_CLOCK_UPDATE_MIN_RATE_ADJUST, ZX_CLOCK_UPDATE_MAX_RATE_ADJUST, kClockRateAdjustDefault,
         kCustomClockOption, kMonotonicClockOption);

  printf("\n    By default, capture audio using packets of 100.0 msec\n");
  printf("  --%s=<MSECS>\tSpecify the duration (in milliseconds) of each capture packet\n",
         kPacketDurationOption);
  printf("\t\t\t(min %.1f, max %.1f)\n", kMinPacketSizeMsec, kMaxPacketSizeMsec);

  printf("\n    By default, capture until a key is pressed\n");
  printf("  --%s[=<SECS>]\tStop recording after a fixed duration (or keystroke)\n",
         kFileDurationOption);
  printf("\t\t\t(min 0.0, max %.1f, default %.1f)\n", kMaxFileDurationSecs,
         kDefaultFileDurationSecs);

  printf("\n  --%s\t\tCapture from an ultrasound capturer\n", kUltrasoundOption);

  printf("\n  --%s\t\t\tDisplay per-packet information\n", kVerboseOption);
  printf("  --%s, --%s\t\tShow this message\n", kShowUsageOption1, kShowUsageOption2);
  printf("\n");
}

bool WavRecorder::Shutdown() {
  gain_control_.Unbind();
  audio_capturer_.Unbind();

  if (clean_shutdown_) {
    CLI_CHECK(wav_writer_.Close(), "file close failed.");
    printf("done.\n");
  } else {
    if (wav_writer_initialized_) {
      CLI_CHECK(wav_writer_.Delete(), "Could not delete WAV file.");
    }
  }

  quit_callback_();
  return false;
}

void WavRecorder::SetupPayloadBuffer() {
  frames_per_packet_ = (packet_duration_ * frames_per_second_) / ZX_SEC(1);

  packets_per_payload_buf_ = std::ceil(static_cast<float>(frames_per_second_) / frames_per_packet_);
  payload_buf_frames_ = frames_per_packet_ * packets_per_payload_buf_;
  payload_buf_size_ = payload_buf_frames_ * bytes_per_frame_;
  CLI_CHECK(payload_buf_size_, "payload_buf_size must be non-zero");

  auto status = zx::vmo::create(payload_buf_size_, 0, &payload_buf_vmo_);
  CLI_CHECK_OK(status, "Failed to create " << payload_buf_size_ << "-byte payload buffer");

  uintptr_t tmp;
  status =
      zx::vmar::root_self()->map(0, payload_buf_vmo_, 0, payload_buf_size_, ZX_VM_PERM_READ, &tmp);
  CLI_CHECK_OK(status, "Failed to map " << payload_buf_size_ << "-byte payload buffer");
  payload_buf_virt_ = reinterpret_cast<void*>(tmp);
}

void WavRecorder::SendCaptureJob() {
  CLI_CHECK(payload_buf_frame_offset_ < payload_buf_frames_,
            "payload_buf_frame_offset:" << payload_buf_frame_offset_
                                        << " must < payload_buf_frames:" << payload_buf_frames_);
  CLI_CHECK((payload_buf_frame_offset_ + frames_per_packet_) <= payload_buf_frames_,
            "payload_buf_frame_offset:" << payload_buf_frame_offset_
                                        << " + frames_per_packet:" << frames_per_packet_
                                        << " must <= payload_buf_frames:" << payload_buf_frames_);

  ++outstanding_capture_jobs_;

  // clang-format off
  audio_capturer_->CaptureAt(kPayloadBufferId,
      payload_buf_frame_offset_,
      frames_per_packet_,
      [this](fuchsia::media::StreamPacket packet) {
        OnPacketProduced(packet);
      });
  // clang-format on

  payload_buf_frame_offset_ += frames_per_packet_;
  if (payload_buf_frame_offset_ >= payload_buf_frames_) {
    payload_buf_frame_offset_ = 0u;
  }
}

// Set the ref clock if requested, then retrieve ref clock and continue when callback is received
void WavRecorder::EstablishReferenceClock() {
  if (clock_type_ != ClockType::Default) {
    // With any of these 3 options, we will first set a reference clock before we retrieve it
    zx::clock reference_clock_to_set;

    // To use the flexible clock, pass a clock with HANDLE_INVALID
    if (clock_type_ == ClockType::Flexible) {
      reference_clock_to_set = zx::clock(ZX_HANDLE_INVALID);
    } else {
      zx_status_t status;
      zx::clock::update_args args;
      args.reset();
      if (adjusting_clock_rate_) {
        args.set_rate_adjust(clock_rate_adjustment_);
      }

      // In both Monotonic and Custom cases, Create, reduce rights, then send to SetRefClock().
      if (clock_type_ == ClockType::Monotonic) {
        // This clock is already started, in lock-step with CLOCK_MONOTONIC.
        reference_clock_to_set = audio::clock::AdjustableCloneOfMonotonic();
        CLI_CHECK(reference_clock_to_set.is_valid(),
                  "Invalid clock; could not clone monotonic clock");
      } else {
        // In custom clock case, set it to start at value zero. Rate-adjust it if specified.
        status = zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr,
                                   &reference_clock_to_set);
        CLI_CHECK_OK(status, "zx::clock::create failed");

        args.set_value(zx::time(0));
      }
      if (adjusting_clock_rate_ || clock_type_ == ClockType::Custom) {
        // update starts our clock
        status = reference_clock_to_set.update(args);
        CLI_CHECK_OK(status, "zx::clock::update failed");
      }

      // The clock we send to AudioCapturer cannot have ZX_RIGHT_WRITE. Most clients would retain
      // their custom clocks for subsequent rate-adjustment, and thus would use 'duplicate' to
      // create the rights-reduced clock. This app doesn't yet allow rate-adjustment during capture
      // (we also don't need this clock to read the current ref time: we call GetReferenceClock
      // later), so we use 'replace' (not 'duplicate').
      auto rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
      status = reference_clock_to_set.replace(rights, &reference_clock_to_set);
      CLI_CHECK_OK(status, "zx::clock::duplicate failed");
    }

    audio_capturer_->SetReferenceClock(std::move(reference_clock_to_set));
  }

  // we receive the reference clock later in ReceiveClockAndContinue
  audio_capturer_->GetReferenceClock(
      [this](zx::clock received_clock) { ReceiveClockAndContinue(std::move(received_clock)); });
}

// Once we've received the reference clock, request the default format and continue
void WavRecorder::ReceiveClockAndContinue(
    zx::clock received_clock, std::optional<fuchsia::media::AudioStreamType> stream_type) {
  reference_clock_ = std::move(received_clock);

  if (verbose_) {
    audio::clock::GetAndDisplayClockDetails(reference_clock_);
  }

  if (stream_type) {
    OnDefaultFormatFetched(*stream_type);
  } else {
    // Fetch the initial media type and figure out what we need to do from there.
    audio_capturer_->GetStreamType([this](fuchsia::media::StreamType type) {
      CLI_CHECK(type.medium_specific.is_audio(), "Default format is not audio!");
      OnDefaultFormatFetched(type.medium_specific.audio());
    });
  }
}

// Once we receive the default format, we don't need to wait for anything else.
// We open our .wav file for recording, set our capture format, set input gain,
// setup our VMO and add it as a payload buffer, send a series of empty packets
void WavRecorder::OnDefaultFormatFetched(const fuchsia::media::AudioStreamType& fmt) {
  auto cleanup = fit::defer([this]() { Shutdown(); });

  channel_count_ = fmt.channels;
  frames_per_second_ = fmt.frames_per_second;

  bool change_format = false;
  bool change_gain = false;
  bool set_mute = false;

  if (fmt.sample_format != sample_format_) {
    change_format = true;
  }

  std::string opt;
  if (cmd_line_.GetOptionValue(kFrameRateOption, &opt)) {
    uint32_t rate;
    CLI_CHECK(sscanf(opt.c_str(), "%u", &rate) == 1, "Frame rate must be a positive integer");
    CLI_CHECK(rate >= fuchsia::media::MIN_PCM_FRAMES_PER_SECOND &&
                  rate <= fuchsia::media::MAX_PCM_FRAMES_PER_SECOND,
              "Frame rate must be between " << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND << " and "
                                            << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);

    if (frames_per_second_ != rate) {
      frames_per_second_ = rate;
      change_format = true;
    }
  }

  if (cmd_line_.HasOption(kGainOption)) {
    stream_gain_db_ = kDefaultCaptureGainDb;

    if (cmd_line_.GetOptionValue(kGainOption, &opt)) {
      if (opt == "") {
        printf("Setting gain to the default %.3f dB\n", stream_gain_db_);
      } else {
        CLI_CHECK(sscanf(opt.c_str(), "%f", &stream_gain_db_) == 1, "Gain must be numeric");
        CLI_CHECK(stream_gain_db_ >= fuchsia::media::audio::MUTED_GAIN_DB &&
                      stream_gain_db_ <= fuchsia::media::audio::MAX_GAIN_DB,
                  "Gain must be between " << fuchsia::media::audio::MUTED_GAIN_DB << " and "
                                          << fuchsia::media::audio::MAX_GAIN_DB);
      }
    }
    change_gain = true;
  }

  if (cmd_line_.HasOption(kMuteOption)) {
    stream_mute_ = true;
    if (cmd_line_.GetOptionValue(kMuteOption, &opt) && opt != "") {
      uint32_t mute_val;
      CLI_CHECK(sscanf(opt.c_str(), "%u", &mute_val) == 1, "Unable to read Mute value");
      stream_mute_ = (mute_val != 0u);
    }
    set_mute = true;
  }

  if (cmd_line_.GetOptionValue(kChannelsOption, &opt)) {
    uint32_t count;
    CLI_CHECK(sscanf(opt.c_str(), "%u", &count) == 1, "Channels must be a positive integer");
    CLI_CHECK((count >= fuchsia::media::MIN_PCM_CHANNEL_COUNT) &&
                  (count <= fuchsia::media::MAX_PCM_CHANNEL_COUNT),
              "Channels must be between " << fuchsia::media::MIN_PCM_CHANNEL_COUNT << " and "
                                          << fuchsia::media::MAX_PCM_CHANNEL_COUNT);

    if (channel_count_ != count) {
      channel_count_ = count;
      change_format = true;
    }
  }

  uint32_t bytes_per_sample =
      (sample_format_ == fuchsia::media::AudioSampleFormat::FLOAT)             ? sizeof(float)
      : (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32) ? sizeof(int32_t)
                                                                               : sizeof(int16_t);
  bytes_per_frame_ = channel_count_ * bytes_per_sample;
  uint32_t bits_per_sample = bytes_per_sample * 8;
  if (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32 &&
      pack_24bit_samples_ == true) {
    bits_per_sample = 24;
  }

  // If desired format differs from default capturer format, change formats now.
  if (change_format) {
    audio_capturer_->SetPcmStreamType(
        media::CreateAudioStreamType(sample_format_, channel_count_, frames_per_second_));
  }

  // Set the specified gain (if specified) for the recording.
  if (change_gain) {
    gain_control_->SetGain(stream_gain_db_);
  }
  if (set_mute) {
    gain_control_->SetMute(stream_mute_);
  }

  // Check whether the user wanted a specific duration for each capture packet.
  if (cmd_line_.GetOptionValue(kPacketDurationOption, &opt)) {
    double packet_size_msec;
    CLI_CHECK(sscanf(opt.c_str(), "%lf", &packet_size_msec) == 1, "Unable to read packet size");
    CLI_CHECK(
        packet_size_msec >= kMinPacketSizeMsec && packet_size_msec <= kMaxPacketSizeMsec,
        "Packet size must be between " << kMinPacketSizeMsec << " and " << kMaxPacketSizeMsec);
    // Don't simply ZX_MSEC(packet_size_msec): that discards any fractional component
    packet_duration_ = packet_size_msec * ZX_MSEC(1);
  }

  // Create a shared payload buffer, map it, dup the handle and pass it to the capturer to fill.
  SetupPayloadBuffer();

  zx::vmo audio_capturer_vmo;
  auto status = payload_buf_vmo_.duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &audio_capturer_vmo);
  CLI_CHECK_OK(status, "Failed to duplicate VMO handle");

  audio_capturer_->AddPayloadBuffer(kPayloadBufferId, std::move(audio_capturer_vmo));

  if (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32) {
    CLI_CHECK(bits_per_sample == (pack_24bit_samples_ ? 24 : 32),
              "Incorrect bits_per_sample value");

    if (pack_24bit_samples_ == true) {
      compress_32_24_buff_ = std::make_unique<uint8_t[]>(payload_buf_size_ * 3 / 4);
    }
  }

  if (cmd_line_.HasOption(kAsyncModeOption)) {
    CLI_CHECK(
        payload_buf_frames_ && frames_per_packet_ && !(payload_buf_frames_ % frames_per_packet_),
        "payload_buf_frames must be a multiple of frames_per_packet; both must be non-zero");
  }

  // Write the inital WAV header
  CLI_CHECK(wav_writer_.Initialize(filename_, sample_format_, channel_count_, frames_per_second_,
                                   bits_per_sample),
            "Could not create the file '" << filename_ << "'");
  wav_writer_initialized_ = true;

  // Will we operate in synchronous or asynchronous mode?  If synchronous, queue
  // all our capture buffers to get the ball rolling. If asynchronous, set an
  // event handler for position notification, and start operating in async mode.
  if (!cmd_line_.HasOption(kAsyncModeOption)) {
    for (size_t i = 0; i < packets_per_payload_buf_; ++i) {
      SendCaptureJob();
    }
  } else {
    audio_capturer_.events().OnPacketProduced = [this](fuchsia::media::StreamPacket pkt) {
      OnPacketProduced(pkt);
    };
    audio_capturer_->StartAsyncCapture(frames_per_packet_);
  }

  // TODO (b/148807692): produce a file with exactly the expected number of frames, or timeout.
  if (file_duration_specifed_) {
    async::PostDelayedTask(
        async_get_default_dispatcher(), [this] { OnQuit(); }, file_duration_);
  }

  printf("\nRecording %s, %u Hz, %u-channel linear PCM\n",
         sample_format_ == fuchsia::media::AudioSampleFormat::FLOAT ? "32-bit float"
         : sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32
             ? (pack_24bit_samples_ ? "packed 24-bit signed int" : "24-bit-in-32-bit signed int")
             : "16-bit signed int",
         frames_per_second_, channel_count_);

  printf("from %s into '%s'\n", loopback_ ? "loopback" : "default input", filename_);

  if (clock_type_ == ClockType::Flexible) {
    printf("using AudioCore's flexible clock as the reference\n");
  } else if (clock_type_ == ClockType::Monotonic) {
    printf("using a clone of CLOCK_MONOTONIC as reference clock");
    if (adjusting_clock_rate_) {
      printf(", adjusting its rate by %d ppm", clock_rate_adjustment_);
    }
    printf("\n");
  } else if (clock_type_ == ClockType::Custom) {
    printf("using a custom reference clock");
    if (adjusting_clock_rate_) {
      printf(", adjusting its rate by %d ppm", clock_rate_adjustment_);
    }
    printf("\n");
  } else {
    printf("using the default reference clock\n");
  }

  printf("using %u packets of %u frames (%.3lf msec) in a %.3f-sec payload buffer\n",
         packets_per_payload_buf_, frames_per_packet_,
         (static_cast<float>(frames_per_packet_) / frames_per_second_) * 1000.0f,
         (static_cast<float>(payload_buf_frames_) / frames_per_second_));
  if (change_gain) {
    printf("applying gain of %.2f dB ", stream_gain_db_);
  }
  if (set_mute) {
    printf("after setting stream Mute to %s", stream_mute_ ? "TRUE" : "FALSE");
  }
  printf("\n");

  cleanup.cancel();
}

constexpr size_t kTimeStrLen = 23;
void WavRecorder::TimeToStr(int64_t time, char* time_str) {
  if (time == fuchsia::media::NO_TIMESTAMP) {
    strncpy(time_str, "          NO_TIMESTAMP", kTimeStrLen - 1);
  } else {
    sprintf(time_str, "%10lu'%03ld'%03ld'%03ld", time / ZX_SEC(1), (time / ZX_MSEC(1)) % 1000,
            (time / ZX_USEC(1)) % 1000, time % ZX_USEC(1));
  }
  time_str[kTimeStrLen - 1] = 0;
}

void WavRecorder::DisplayPacket(fuchsia::media::StreamPacket pkt) {
  if (pkt.flags & fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY) {
    printf("       ****  DISCONTINUITY REPORTED  ****\n");
  }

  char duration_str[9];
  if (pkt.payload_size) {
    sprintf(duration_str, "- %6lu", pkt.payload_offset + pkt.payload_size - 1);
  } else {
    strncpy(duration_str, " (empty)", 8);
  }
  duration_str[8] = 0;

  char pts_str[kTimeStrLen];
  TimeToStr(pkt.pts, pts_str);

  zx_time_t ref_now;
  auto status = reference_clock_.read(&ref_now);
  auto mono_now = zx::clock::get_monotonic().get();
  CLI_CHECK(status == ZX_OK || Shutdown(), "reference_clock_.read failed");

  char ref_now_str[kTimeStrLen], mono_now_str[kTimeStrLen];
  TimeToStr(ref_now, ref_now_str);
  TimeToStr(mono_now, mono_now_str);

  printf("PACKET [%6lu %s ] flags 0x%02x : ts %s : ref_now %s : mono_now %s\n", pkt.payload_offset,
         duration_str, pkt.flags, pts_str, ref_now_str, mono_now_str);
}

// A packet containing captured audio data was just returned to us -- handle it.
void WavRecorder::OnPacketProduced(fuchsia::media::StreamPacket pkt) {
  if (verbose_) {
    DisplayPacket(pkt);
  }

  // If operating in sync-mode, track how many submitted packets are pending.
  if (audio_capturer_.events().OnPacketProduced == nullptr) {
    --outstanding_capture_jobs_;
  }

  CLI_CHECK((pkt.payload_offset + pkt.payload_size) <= (payload_buf_frames_ * bytes_per_frame_) ||
                Shutdown(),
            "pkt.payload_offset:" << pkt.payload_offset
                                  << " + pkt.payload_size:" << pkt.payload_size << " too large");

  if (pkt.payload_size) {
    CLI_CHECK(payload_buf_virt_ || Shutdown(), "payload_buf_virt cannot be null");

    auto tgt = reinterpret_cast<uint8_t*>(payload_buf_virt_) + pkt.payload_offset;

    uint32_t write_size = pkt.payload_size;
    // If 24_in_32, write as packed-24, skipping the first, least-significant of
    // each four bytes). Assuming Write does not buffer, compress locally and
    // call Write just once, to avoid potential performance problems.
    if (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32 &&
        pack_24bit_samples_) {
      uint32_t write_idx = 0;
      uint32_t read_idx = 0;
      while (read_idx < pkt.payload_size) {
        ++read_idx;
        compress_32_24_buff_[write_idx++] = tgt[read_idx++];
        compress_32_24_buff_[write_idx++] = tgt[read_idx++];
        compress_32_24_buff_[write_idx++] = tgt[read_idx++];
      }
      write_size = write_idx;
      tgt = compress_32_24_buff_.get();
    }

    if (!wav_writer_.Write(reinterpret_cast<void* const>(tgt), write_size)) {
      printf("File write failed. Trying to save any already-written data.\n");
      CLI_CHECK(wav_writer_.Close(), "File close failed as well.");
      Shutdown();
    }
  }

  // In sync-mode, we send/track packets as they are sent/returned.
  if (audio_capturer_.events().OnPacketProduced == nullptr) {
    // If not shutting down, then send another capture job to keep things going.
    if (!clean_shutdown_) {
      SendCaptureJob();
    }
    // ...else (if shutting down) wait for pending capture jobs, then Shutdown.
    else if (outstanding_capture_jobs_ == 0) {
      Shutdown();
    }
  }
}

// On receiving the key-press to quit, start the sequence of unwinding.
void WavRecorder::OnQuit() {
  if (!clean_shutdown_) {
    clean_shutdown_ = true;
    printf("Shutting down...\n");

    // If async-mode, we can shutdown now (need not wait for packets to return).
    if (audio_capturer_.events().OnPacketProduced != nullptr) {
      audio_capturer_->StopAsyncCaptureNoReply();
      Shutdown();
    }
    // If operating in sync-mode, wait for all packets to return, then Shutdown.
    else {
      audio_capturer_->DiscardAllPacketsNoReply();
    }
  }
}

}  // namespace media::tools
