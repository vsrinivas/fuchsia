// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/tools/wav_recorder/wav_recorder.h"

#include <lib/async-loop/loop.h>
#include <lib/fit/defer.h>
#include <poll.h>
#include <unistd.h>

#include "lib/media/audio/cpp/types.h"
#include "src/lib/fxl/logging.h"
#include "src/media/audio/lib/wav_writer/wav_writer.h"

namespace media::tools {

// TODO(mpuryear): make these constexpr char[] and eliminate c_str() usage later
static const std::string kLoopbackOption = "loopback";
static const std::string kChannelsOption = "chans";
static const std::string kFrameRateOption = "rate";
static const std::string kFloatFormatOption = "float";
static const std::string k24In32FormatOption = "int24";
static const std::string kPacked24FormatOption = "packed24";
static const std::string kGainOption = "gain";
static const std::string kMuteOption = "mute";
static const std::string kAsyncModeOption = "async";
static const std::string kVerboseOption = "v";
static const std::string kShowUsageOption1 = "help";
static const std::string kShowUsageOption2 = "?";

constexpr zx_duration_t kCaptureChunkDuration = ZX_MSEC(100);
constexpr size_t kCaptureChunkCount = 10;

WavRecorder::~WavRecorder() {
  if (payload_buf_virt_ != nullptr) {
    FXL_DCHECK(payload_buf_size_ != 0);
    FXL_DCHECK(bytes_per_frame_ != 0);
    zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(payload_buf_virt_),
                                 payload_buf_size_);
  }
}

void WavRecorder::Run(sys::ComponentContext* app_context) {
  auto cleanup = fit::defer([this]() { Shutdown(); });
  const auto& pos_args = cmd_line_.positional_args();

  // Parse our args.
  if (cmd_line_.HasOption(kShowUsageOption1) ||
      cmd_line_.HasOption(kShowUsageOption2)) {
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
  fuchsia::media::AudioPtr audio =
      app_context->svc()->Connect<fuchsia::media::Audio>();

  audio->CreateAudioCapturer(audio_capturer_.NewRequest(), loopback_);
  audio_capturer_->BindGainControl(gain_control_.NewRequest());
  audio_capturer_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR)
        << "Client connection to fuchsia.media.AudioCapturer failed: "
        << status;
    Shutdown();
  });
  gain_control_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Client connection to fuchsia.media.GainControl failed: "
                   << status;
    Shutdown();
  });

  // Fetch the initial media type and figure out what we need to do from there.
  audio_capturer_->GetStreamType([this](fuchsia::media::StreamType type) {
    OnDefaultFormatFetched(std::move(type));
  });

  // Quit if someone hits a key.
  keystroke_waiter_.Wait([this](zx_status_t, uint32_t) { OnQuit(); },
                         STDIN_FILENO, POLLIN);

  cleanup.cancel();
}

void WavRecorder::Usage() {
  printf("\nUsage: %s [options] <filename>\n", cmd_line_.argv0().c_str());
  printf("Record an audio signal from the specified source, to a .wav file.\n");
  printf("\nValid options:\n");

  printf("\n   By default, use the preferred input device\n");
  printf(" --%s\t\tCapture final-mix output from the preferred output device\n",
         kLoopbackOption.c_str());

  printf(
      "\n   By default, use device-preferred channel count and frame rate, in "
      "16-bit integer samples\n");
  printf(" --%s=<NUM_CHANS>\tSpecify the number of channels (in [%u, %u])\n",
         kChannelsOption.c_str(), fuchsia::media::MIN_PCM_CHANNEL_COUNT,
         fuchsia::media::MAX_PCM_CHANNEL_COUNT);
  printf(" --%s=<rate>\t\tSpecify the capture frame rate (Hz in [%u, %u])\n",
         kFrameRateOption.c_str(), fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
         fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
  printf(" --%s\t\tRecord and save as 32-bit float\n",
         kFloatFormatOption.c_str());
  printf(
      " --%s\t\tRecord and save as left-justified 24-in-32 int ('padded-24')\n",
      k24In32FormatOption.c_str());
  printf(" --%s\t\tRecord as 24-in-32 'padded-24'; save as 'packed-24'\n",
         kPacked24FormatOption.c_str());

  printf(
      "\n   By default, don't set AudioCapturer gain and mute (unity 0 dB, "
      "unmuted)\n");
  printf(
      " --%s[=<GAIN_DB>]\tSet stream gain (dB in [%.1f, +%.1f]; 0.0 if only "
      "'--%s' is provided)\n",
      kGainOption.c_str(), fuchsia::media::audio::MUTED_GAIN_DB,
      fuchsia::media::audio::MAX_GAIN_DB, kGainOption.c_str());
  printf(
      " --%s[=<0|1>]\t\tSet stream mute (0=Unmute or 1=Mute; Mute if only "
      "'--%s' is provided)\n",
      kMuteOption.c_str(), kMuteOption.c_str());

  printf("\n   By default, use packet-by-packet ('synchronous') mode\n");
  printf(" --%s\t\tCapture using sequential-buffer ('asynchronous') mode\n",
         kAsyncModeOption.c_str());

  printf("\n --%s\t\t\tBe verbose; display per-packet info\n",
         kVerboseOption.c_str());
  printf(" --%s, --%s\t\tShow this message\n", kShowUsageOption1.c_str(),
         kShowUsageOption2.c_str());
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
  capture_frames_per_chunk_ =
      (kCaptureChunkDuration * frames_per_second_) / ZX_SEC(1);
  payload_buf_frames_ = capture_frames_per_chunk_ * kCaptureChunkCount;
  payload_buf_size_ = payload_buf_frames_ * bytes_per_frame_;

  zx_status_t res;
  res = zx::vmo::create(payload_buf_size_, 0, &payload_buf_vmo_);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create " << payload_buf_size_
                   << " byte payload buffer (res " << res << ")";
    return false;
  }

  uintptr_t tmp;
  res = zx::vmar::root_self()->map(0, payload_buf_vmo_, 0, payload_buf_size_,
                                   ZX_VM_PERM_READ, &tmp);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map " << payload_buf_size_
                   << " byte payload buffer (res " << res << ")";
    return false;
  }
  payload_buf_virt_ = reinterpret_cast<void*>(tmp);

  return true;
}

void WavRecorder::SendCaptureJob() {
  FXL_DCHECK(capture_frame_offset_ < payload_buf_frames_);
  FXL_DCHECK((capture_frame_offset_ + capture_frames_per_chunk_) <=
             payload_buf_frames_);

  ++outstanding_capture_jobs_;

  // clang-format off
  audio_capturer_->CaptureAt(0,
      capture_frame_offset_,
      capture_frames_per_chunk_,
      [this](fuchsia::media::StreamPacket packet) {
        OnPacketProduced(packet);
      });
  // clang-format on

  capture_frame_offset_ += capture_frames_per_chunk_;
  if (capture_frame_offset_ >= payload_buf_frames_) {
    capture_frame_offset_ = 0u;
  }
}

// Once we receive the default format, we don't need to wait for anything else.
// We open our .wav file for recording, set our capture format, set input gain,
// setup our VMO and add it as a payload buffer, send a series of empty packets
void WavRecorder::OnDefaultFormatFetched(fuchsia::media::StreamType type) {
  auto cleanup = fit::defer([this]() { Shutdown(); });
  zx_status_t res;

  if (!type.medium_specific.is_audio()) {
    FXL_LOG(ERROR) << "Default format is not audio!";
    return;
  }

  const auto& fmt = type.medium_specific.audio();

  // If user erroneously specifies float AND 24-in-32, prefer float.
  if (cmd_line_.HasOption(kFloatFormatOption)) {
    sample_format_ = fuchsia::media::AudioSampleFormat::FLOAT;
  } else if (cmd_line_.HasOption(kPacked24FormatOption)) {
    pack_24bit_samples_ = true;
    sample_format_ = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
  } else if (cmd_line_.HasOption(k24In32FormatOption)) {
    sample_format_ = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
  } else {
    sample_format_ = fuchsia::media::AudioSampleFormat::SIGNED_16;
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
    if (::sscanf(opt.c_str(), "%u", &rate) != 1) {
      Usage();
      return;
    }

    if ((rate < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) ||
        (rate > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)) {
      printf("Frame rate (%u) must be within range [%u, %u]\n", rate,
             fuchsia::media::MIN_PCM_FRAMES_PER_SECOND,
             fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
      return;
    }

    if (frames_per_second_ != rate) {
      frames_per_second_ = rate;
      change_format = true;
    }
  }

  if (cmd_line_.HasOption(kGainOption)) {
    stream_gain_db_ = 0.0f;
    if (cmd_line_.GetOptionValue(kGainOption, &opt)) {
      if (::sscanf(opt.c_str(), "%f", &stream_gain_db_) != 1) {
        Usage();
        return;
      }

      if ((stream_gain_db_ < fuchsia::media::audio::MUTED_GAIN_DB) ||
          (stream_gain_db_ > fuchsia::media::audio::MAX_GAIN_DB)) {
        printf("Gain (%.3f dB) must be within range [%.1f, %.1f]\n",
               stream_gain_db_, fuchsia::media::audio::MUTED_GAIN_DB,
               fuchsia::media::audio::MAX_GAIN_DB);
        return;
      }
    }
    change_gain = true;
  }

  if (cmd_line_.HasOption(kMuteOption)) {
    stream_mute_ = true;
    if (cmd_line_.GetOptionValue(kMuteOption, &opt)) {
      uint32_t mute_val;
      if (::sscanf(opt.c_str(), "%u", &mute_val) != 1) {
        Usage();
        return;
      }
      stream_mute_ = (mute_val > 0);
    }
    set_mute = true;
  }

  if (cmd_line_.GetOptionValue(kChannelsOption, &opt)) {
    uint32_t count;
    if (::sscanf(opt.c_str(), "%u", &count) != 1) {
      Usage();
      return;
    }

    if ((count < fuchsia::media::MIN_PCM_CHANNEL_COUNT) ||
        (count > fuchsia::media::MAX_PCM_CHANNEL_COUNT)) {
      printf("Channel count (%u) must be within range [%u, %u]\n", count,
             fuchsia::media::MIN_PCM_CHANNEL_COUNT,
             fuchsia::media::MAX_PCM_CHANNEL_COUNT);
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
          : (sample_format_ ==
             fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32)
                ? sizeof(int32_t)
                : sizeof(int16_t);
  bytes_per_frame_ = channel_count_ * bytes_per_sample;
  uint32_t bits_per_sample = bytes_per_sample * 8;
  if (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32 &&
      pack_24bit_samples_ == true) {
    bits_per_sample = 24;
  }

  // Write the inital WAV header
  if (!wav_writer_.Initialize(filename_, sample_format_, channel_count_,
                              frames_per_second_, bits_per_sample)) {
    return;
  }

  // If desired format differs from default capturer format, change formats now.
  if (change_format) {
    audio_capturer_->SetPcmStreamType(media::CreateAudioStreamType(
        sample_format_, channel_count_, frames_per_second_));
  }

  // Set the specified gain (if specified) for the recording.
  if (change_gain) {
    gain_control_->SetGain(stream_gain_db_);
  }
  if (set_mute) {
    gain_control_->SetMute(stream_mute_);
  }

  // Create our shared payload buffer, map it into place, then dup the handle
  // and pass it on to the capturer to fill.
  if (!SetupPayloadBuffer()) {
    return;
  }

  zx::vmo audio_capturer_vmo;
  res = payload_buf_vmo_.duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP,
      &audio_capturer_vmo);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate VMO handle (res " << res << ")";
    return;
  }
  audio_capturer_->AddPayloadBuffer(0, std::move(audio_capturer_vmo));

  // Will we operate in synchronous or asynchronous mode?  If synchronous, queue
  // all our capture buffers to get the ball rolling. If asynchronous, set an
  // event handler for position notification, and start operating in async mode.
  if (!cmd_line_.HasOption(kAsyncModeOption)) {
    for (size_t i = 0; i < kCaptureChunkCount; ++i) {
      SendCaptureJob();
    }
  } else {
    FXL_DCHECK(payload_buf_frames_);
    FXL_DCHECK(capture_frames_per_chunk_);
    FXL_DCHECK((payload_buf_frames_ % capture_frames_per_chunk_) == 0);
    audio_capturer_.events().OnPacketProduced =
        [this](fuchsia::media::StreamPacket pkt) { OnPacketProduced(pkt); };
    audio_capturer_->StartAsyncCapture(capture_frames_per_chunk_);
  }

  if (sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32) {
    FXL_DCHECK(bits_per_sample == (pack_24bit_samples_ ? 24 : 32));
    if (pack_24bit_samples_ == true) {
      compress_32_24_buff_ =
          std::make_unique<uint8_t[]>(payload_buf_size_ * 3 / 4);
    }
  }

  printf(
      "Recording %s, %u Hz, %u-channel linear PCM\n",
      sample_format_ == fuchsia::media::AudioSampleFormat::FLOAT
          ? "32-bit float"
          : sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32
                ? (pack_24bit_samples_ ? "packed 24-bit signed int"
                                       : "24-bit-in-32-bit signed int")
                : "16-bit signed int",
      frames_per_second_, channel_count_);
  printf("from %s into '%s'", loopback_ ? "loopback" : "default input",
         filename_);
  if (change_gain) {
    printf(", applying gain of %.2f dB", stream_gain_db_);
  }
  if (set_mute) {
    printf(", after setting stream Mute to %s",
           stream_mute_ ? "TRUE" : "FALSE");
  }
  printf("\n");

  cleanup.cancel();
}

// A packet containing captured audio data was just returned to us -- handle it.
void WavRecorder::OnPacketProduced(fuchsia::media::StreamPacket pkt) {
  if (verbose_) {
    printf("PACKET [%6lu, %6lu] flags 0x%02x : ts %ld\n", pkt.payload_offset,
           pkt.payload_size, pkt.flags, pkt.pts);
  }

  // If operating in sync-mode, track how many submitted packets are pending.
  if (audio_capturer_.events().OnPacketProduced == nullptr) {
    --outstanding_capture_jobs_;
  }

  FXL_DCHECK((pkt.payload_offset + pkt.payload_size) <=
             (payload_buf_frames_ * bytes_per_frame_));

  if (pkt.payload_size) {
    FXL_DCHECK(payload_buf_virt_);

    auto tgt =
        reinterpret_cast<uint8_t*>(payload_buf_virt_) + pkt.payload_offset;

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
