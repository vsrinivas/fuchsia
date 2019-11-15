// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/tools/wav_recorder/wav_recorder.h"

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <poll.h>
#include <unistd.h>

#include "lib/media/audio/cpp/types.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/lib/wav_writer/wav_writer.h"

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
constexpr char kPacketDurationOption[] = "packet-ms";
constexpr char kVerboseOption[] = "v";
constexpr char kShowUsageOption1[] = "help";
constexpr char kShowUsageOption2[] = "?";

WavRecorder::~WavRecorder() {
  if (payload_buf_virt_ != nullptr) {
    FX_DCHECK(payload_buf_size_ != 0);
    FX_DCHECK(bytes_per_frame_ != 0);
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

  if (pos_args.size() < 1) {
    Usage();
    return;
  }

  filename_ = pos_args[0].c_str();

  // Connect to the audio service and obtain AudioCapturer and Gain interfaces.
  fuchsia::media::AudioPtr audio = app_context->svc()->Connect<fuchsia::media::Audio>();

  audio->CreateAudioCapturer(audio_capturer_.NewRequest(), loopback_);
  audio_capturer_->BindGainControl(gain_control_.NewRequest());
  audio_capturer_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Client connection to fuchsia.media.AudioCapturer failed: " << status;
    Shutdown();
  });
  gain_control_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Client connection to fuchsia.media.GainControl failed: " << status;
    Shutdown();
  });

  // Fetch the initial media type and figure out what we need to do from there.
  audio_capturer_->GetStreamType(
      [this](fuchsia::media::StreamType type) { OnDefaultFormatFetched(std::move(type)); });

  // Quit if someone hits a key.
  keystroke_waiter_.Wait([this](zx_status_t, uint32_t) { OnQuit(); }, STDIN_FILENO, POLLIN);

  cleanup.cancel();
}

void WavRecorder::Usage() {
  printf("\nUsage: %s [options] <filename>\n", cmd_line_.argv0().c_str());
  printf("Record an audio signal from the specified source, to a .wav file.\n");
  printf("\nValid options:\n");

  printf("\n   By default, use the preferred input device\n");
  printf(" --%s\t\tCapture final-mix output from the preferred output device\n", kLoopbackOption);

  printf(
      "\n   By default, use device-preferred channel count and frame rate, in 32-bit float "
      "samples\n");
  printf(" --%s=<NUM_CHANS>\tSpecify the number of channels (in [%u, %u])\n", kChannelsOption,
         fuchsia::media::MIN_PCM_CHANNEL_COUNT, fuchsia::media::MAX_PCM_CHANNEL_COUNT);
  printf(" --%s=<rate>\t\tSpecify the capture frame rate (Hz in [%u, %u])\n", kFrameRateOption,
         fuchsia::media::MIN_PCM_FRAMES_PER_SECOND, fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
  printf(" --%s\t\tRecord and save as left-justified 24-in-32 int ('padded-24')\n",
         k24In32FormatOption);
  printf(" --%s\t\tRecord as 24-in-32 'padded-24'; save as 'packed-24'\n", kPacked24FormatOption);
  printf(" --%s\t\tRecord and save as 16-bit integer\n", kInt16FormatOption);

  printf("\n   By default, don't set AudioCapturer gain and mute (unity 0 dB, unmuted)\n");
  printf(
      " --%s[=<GAIN_DB>]\tSet stream gain (dB in [%.1f, +%.1f]; %.1f if only '--%s' is provided)\n",
      kGainOption, fuchsia::media::audio::MUTED_GAIN_DB, fuchsia::media::audio::MAX_GAIN_DB,
      kDefaultCaptureGainDb, kGainOption);
  printf(" --%s[=<0|1>]\t\tSet stream mute (0=Unmute or 1=Mute; Mute if only '--%s' is provided)\n",
         kMuteOption, kMuteOption);

  printf("\n   By default, use packet-by-packet ('synchronous') mode\n");
  printf(" --%s\t\tCapture using sequential-buffer ('asynchronous') mode\n", kAsyncModeOption);

  printf("\n   By default, capture audio using packets of 100.0 msec\n");
  printf(" --%s=<MSECS>\tSpecify the duration (in milliseconds) of each capture packet\n",
         kPacketDurationOption);
  printf("\t\t\tMinimum packet duration is %.1f milliseconds\n", kMinPacketSizeMsec);

  printf("\n --%s\t\t\tBe verbose; display per-packet info\n", kVerboseOption);
  printf(" --%s, --%s\t\tShow this message\n", kShowUsageOption1, kShowUsageOption2);
  printf("\n");
}

void WavRecorder::Shutdown() {
  if (gain_control_.is_bound()) {
    gain_control_.set_error_handler(nullptr);
    gain_control_.Unbind();
  }
  if (audio_capturer_.is_bound()) {
    audio_capturer_.set_error_handler(nullptr);
    audio_capturer_.Unbind();
  }

  if (clean_shutdown_) {
    if (wav_writer_.Close()) {
      printf("done.\n");
    } else {
      printf("file close failed.\n");
    }
  } else {
    if (!wav_writer_.Delete()) {
      printf("Could not delete WAV file.\n");
    }
  }

  quit_callback_();
}

bool WavRecorder::SetupPayloadBuffer() {
  frames_per_packet_ = (packet_duration_ * frames_per_second_) / ZX_SEC(1);

  packets_per_payload_buf_ = std::ceil(static_cast<float>(frames_per_second_) / frames_per_packet_);
  payload_buf_frames_ = frames_per_packet_ * packets_per_payload_buf_;
  payload_buf_size_ = payload_buf_frames_ * bytes_per_frame_;

  zx_status_t res;
  res = zx::vmo::create(payload_buf_size_, 0, &payload_buf_vmo_);
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create " << payload_buf_size_ << " byte payload buffer (res "
                   << res << ")";
    return false;
  }

  uintptr_t tmp;
  res =
      zx::vmar::root_self()->map(0, payload_buf_vmo_, 0, payload_buf_size_, ZX_VM_PERM_READ, &tmp);
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map " << payload_buf_size_ << " byte payload buffer (res " << res
                   << ")";
    return false;
  }
  payload_buf_virt_ = reinterpret_cast<void*>(tmp);

  return true;
}

void WavRecorder::SendCaptureJob() {
  FX_DCHECK(payload_buf_frame_offset_ < payload_buf_frames_);
  FX_DCHECK((payload_buf_frame_offset_ + frames_per_packet_) <= payload_buf_frames_);

  ++outstanding_capture_jobs_;

  // clang-format off
  audio_capturer_->CaptureAt(0,
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

// Once we receive the default format, we don't need to wait for anything else.
// We open our .wav file for recording, set our capture format, set input gain,
// setup our VMO and add it as a payload buffer, send a series of empty packets
void WavRecorder::OnDefaultFormatFetched(fuchsia::media::StreamType type) {
  auto cleanup = fit::defer([this]() { Shutdown(); });
  zx_status_t res;

  if (!type.medium_specific.is_audio()) {
    FX_LOGS(ERROR) << "Default format is not audio!";
    return;
  }

  const auto& fmt = type.medium_specific.audio();

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
    if (sscanf(opt.c_str(), "%u", &rate) != 1) {
      Usage();
      return;
    }

    if ((rate < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) ||
        (rate > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)) {
      printf("Frame rate (%u) must be within range [%u, %u]\n", rate,
             fuchsia::media::MIN_PCM_FRAMES_PER_SECOND, fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
      return;
    }

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
      } else if (sscanf(opt.c_str(), "%f", &stream_gain_db_) != 1) {
        Usage();
        return;
      } else if ((stream_gain_db_ < fuchsia::media::audio::MUTED_GAIN_DB) ||
                 (stream_gain_db_ > fuchsia::media::audio::MAX_GAIN_DB)) {
        printf("Gain (%.3f dB) must be within range [%.1f, %.1f]\n", stream_gain_db_,
               fuchsia::media::audio::MUTED_GAIN_DB, fuchsia::media::audio::MAX_GAIN_DB);

        return;
      }
    }
    change_gain = true;
  }

  if (cmd_line_.HasOption(kMuteOption)) {
    stream_mute_ = true;
    if (cmd_line_.GetOptionValue(kMuteOption, &opt)) {
      uint32_t mute_val;
      if (sscanf(opt.c_str(), "%u", &mute_val) != 1) {
        Usage();
        return;
      }
      stream_mute_ = (mute_val > 0);
    }
    set_mute = true;
  }

  if (cmd_line_.GetOptionValue(kChannelsOption, &opt)) {
    uint32_t count;
    if (sscanf(opt.c_str(), "%u", &count) != 1) {
      Usage();
      return;
    }

    if ((count < fuchsia::media::MIN_PCM_CHANNEL_COUNT) ||
        (count > fuchsia::media::MAX_PCM_CHANNEL_COUNT)) {
      printf("Channel count (%u) must be within range [%u, %u]\n", count,
             fuchsia::media::MIN_PCM_CHANNEL_COUNT, fuchsia::media::MAX_PCM_CHANNEL_COUNT);
      return;
    }

    if (channel_count_ != count) {
      channel_count_ = count;
      change_format = true;
    }
  }

  uint32_t bytes_per_sample =
      (sample_format_ == fuchsia::media::AudioSampleFormat::FLOAT)
          ? sizeof(float)
          : (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32)
                ? sizeof(int32_t)
                : sizeof(int16_t);
  bytes_per_frame_ = channel_count_ * bytes_per_sample;
  uint32_t bits_per_sample = bytes_per_sample * 8;
  if (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32 &&
      pack_24bit_samples_ == true) {
    bits_per_sample = 24;
  }

  // Write the inital WAV header
  if (!wav_writer_.Initialize(filename_, sample_format_, channel_count_, frames_per_second_,
                              bits_per_sample)) {
    return;
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
    if (sscanf(opt.c_str(), "%lf", &packet_size_msec) != 1 ||
        packet_size_msec < kMinPacketSizeMsec) {
      Usage();
      return;
    }
    // Don't simply ZX_MSEC(packet_size_msec): that discards any fractional component
    packet_duration_ = packet_size_msec * ZX_MSEC(1);
  }

  // Create a shared payload buffer, map it, dup the handle and pass it to the capturer to fill.
  if (!SetupPayloadBuffer()) {
    return;
  }

  zx::vmo audio_capturer_vmo;
  res = payload_buf_vmo_.duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &audio_capturer_vmo);
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate VMO handle (res " << res << ")";
    return;
  }
  audio_capturer_->AddPayloadBuffer(0, std::move(audio_capturer_vmo));

  // Will we operate in synchronous or asynchronous mode?  If synchronous, queue
  // all our capture buffers to get the ball rolling. If asynchronous, set an
  // event handler for position notification, and start operating in async mode.
  if (!cmd_line_.HasOption(kAsyncModeOption)) {
    for (size_t i = 0; i < packets_per_payload_buf_; ++i) {
      SendCaptureJob();
    }
  } else {
    FX_DCHECK(payload_buf_frames_);
    FX_DCHECK(frames_per_packet_);
    FX_DCHECK((payload_buf_frames_ % frames_per_packet_) == 0);
    audio_capturer_.events().OnPacketProduced = [this](fuchsia::media::StreamPacket pkt) {
      OnPacketProduced(pkt);
    };
    audio_capturer_->StartAsyncCapture(frames_per_packet_);
  }

  if (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32) {
    FX_DCHECK(bits_per_sample == (pack_24bit_samples_ ? 24 : 32));
    if (pack_24bit_samples_ == true) {
      compress_32_24_buff_ = std::make_unique<uint8_t[]>(payload_buf_size_ * 3 / 4);
    }
  }

  printf(
      "\nRecording %s, %u Hz, %u-channel linear PCM\n",
      sample_format_ == fuchsia::media::AudioSampleFormat::FLOAT
          ? "32-bit float"
          : sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32
                ? (pack_24bit_samples_ ? "packed 24-bit signed int" : "24-bit-in-32-bit signed int")
                : "16-bit signed int",
      frames_per_second_, channel_count_);
  printf("from %s into '%s'\n", loopback_ ? "loopback" : "default input", filename_);
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

  auto now = zx::clock::get_monotonic().get();
  char now_str[kTimeStrLen];
  TimeToStr(now, now_str);

  printf("PACKET [%6lu %s ] flags 0x%02x : ts %s : now %s\n", pkt.payload_offset, duration_str,
         pkt.flags, pts_str, now_str);
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

  FX_DCHECK((pkt.payload_offset + pkt.payload_size) <= (payload_buf_frames_ * bytes_per_frame_));

  if (pkt.payload_size) {
    FX_DCHECK(payload_buf_virt_);

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
      if (!wav_writer_.Close()) {
        printf("File close failed as well.\n");
      }
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
  printf("Shutting down...\n");
  clean_shutdown_ = true;

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

}  // namespace media::tools
