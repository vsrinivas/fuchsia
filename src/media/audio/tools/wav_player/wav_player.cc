// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/tools/wav_player/wav_player.h"

#include <fuchsia/ultrasound/cpp/fidl.h>
#include <poll.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/clock.h>

#include <iomanip>
#include <iostream>

#include <fbl/string_printf.h>

#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/logging/cli.h"

namespace media::tools {

const char* SampleFormatToString(const fuchsia::media::AudioSampleFormat& format) {
  switch (format) {
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return "float32";
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return "int24-in-32";
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return "int16";
    default:
      return "(unknown)";
  }
}

fbl::String RefTimeStrFromZxTime(zx::time zx_time) {
  auto time = zx_time.get();

  if (time == fuchsia::media::NO_TIMESTAMP) {
    return fbl::String("  [NO_TIMESTAMP]   ");
  }
  return fbl::StringPrintf("%07lu'%03lu'%03lu'%03lu", time / ZX_SEC(1),
                           (time % ZX_SEC(1)) / ZX_MSEC(1), (time % ZX_MSEC(1)) / ZX_USEC(1),
                           time % ZX_USEC(1));
}

fbl::String RefTimeMsStrFromZxTime(zx::time zx_time) {
  auto time = zx_time.get();

  if (time == fuchsia::media::NO_TIMESTAMP) {
    return fbl::String("[NO_TIMESTAMP]   ");
  }
  return fbl::StringPrintf("%07lu'%03lu.%02lu ms", time / ZX_SEC(1),
                           (time % ZX_SEC(1)) / ZX_MSEC(1), (time % ZX_MSEC(1)) / ZX_USEC(10));
}

WavPlayer::WavPlayer(Options options) : options_(std::move(options)) {
  CLI_CHECK(options_.quit_callback, "quit_callback must not be null");
}

// Prepare for playback, submit initial data, start the presentation timeline.
void WavPlayer::Run(sys::ComponentContext* app_context) {
  AcquireRenderer(app_context);

  // Create a WavReader and open the WAV file, retrieving its configuration.
  InitializeWavReader();

  // Check the cmdline flags; exit if any are invalid or out-of-range.
  ParameterRangeChecks();

  ConfigureRenderer();
  SetLoudnessLevels(app_context);

  // Calculate the frame size, number of packets, and shared-buffer size.
  SetupPayloadCoefficients();

  // Show a summary of all our settings: exactly what we are about to do.
  DisplayConfigurationSettings();

  // Create VmoMapper(s) that Create+Map a VMO. Send these down via AudioRenderer::AddPayloadBuffer.
  CreateMemoryMapping();

  // Retrieve the default reference clock for this renderer; once a device is ready, start playback.
  GetClockAndStart();

  keystroke_waiter_.Wait([this](zx_status_t, uint32_t) { OnKeyPress(); }, STDIN_FILENO, POLLIN);
}

// Use ComponentContext to acquire AudioPtr; use that to acquire AudioRendererPtr in turn. Set
// AudioRenderer error handler, in case of channel closure.
void WavPlayer::AcquireRenderer(sys::ComponentContext* app_context) {
  if (options_.ultrasound) {
    fuchsia::ultrasound::FactorySyncPtr ultrasound_factory;
    app_context->svc()->Connect(ultrasound_factory.NewRequest());

    zx::clock reference_clock;
    fuchsia::media::AudioStreamType stream_type;
    ultrasound_factory->CreateRenderer(audio_renderer_.NewRequest(), &reference_clock,
                                       &stream_type);
    frame_rate_ = stream_type.frames_per_second;
    num_channels_ = stream_type.channels;
    sample_format_ = stream_type.sample_format;
  } else {
    // Audio interface is needed to create AudioRenderer and set routing policy.
    fuchsia::media::AudioPtr audio;
    app_context->svc()->Connect(audio.NewRequest());

    audio->CreateAudioRenderer(audio_renderer_.NewRequest());
  }
  audio_renderer_.set_error_handler([this](zx_status_t status) {
    CLI_CHECK(Shutdown(), "Client connection to fuchsia.media.AudioRenderer failed: " << status);
  });
}

void WavPlayer::InitializeWavReader() {
  auto file_result = audio::WavReader::Open(options_.file_name);
  if (!file_result.is_ok()) {
    CLI_CHECK(file_result.error() != ZX_ERR_NOT_FOUND,
              "WavReader::Open() failed: file not found\n");
    CLI_CHECK(file_result.error() != ZX_ERR_ACCESS_DENIED,
              "WavReader::Open() failed: access denied\n");
    CLI_CHECK(file_result.error() != ZX_ERR_IO, "WavReader::Open() failed: I/O error\n");
    CLI_CHECK(file_result.is_ok(),
              "WavReader::Open() failed with err " + std::to_string(file_result.error()));
  }

  wav_reader_ = file_result.take_value();

  if (options_.ultrasound) {
    CLI_CHECK(wav_reader_->channel_count() == num_channels_,
              "File channel count (" << wav_reader_->channel_count()
                                     << ") is different than device native channel count ("
                                     << num_channels_ << ")\n");
    CLI_CHECK(wav_reader_->frame_rate() == frame_rate_,
              "File frame rate (" << wav_reader_->frame_rate()
                                  << ") is different than device native frame rate (" << frame_rate_
                                  << ")\n");
    CLI_CHECK(wav_reader_->sample_format() == sample_format_,
              "File sample format (" << SampleFormatToString(wav_reader_->sample_format())
                                     << ") is different than device native sample format ("
                                     << SampleFormatToString(sample_format_) << ")\n");
  } else {
    num_channels_ = wav_reader_->channel_count();
    frame_rate_ = wav_reader_->frame_rate();
    sample_format_ = wav_reader_->sample_format();
  }

  if (wav_reader_->length_in_frames() == 0) {
    stopping_ = true;
    return;
  }
}

void WavPlayer::ParameterRangeChecks() {
  bool success = true;

  if (num_channels_ < fuchsia::media::MIN_PCM_CHANNEL_COUNT) {
    std::cerr << "Number of channels must be at least " << fuchsia::media::MIN_PCM_CHANNEL_COUNT
              << std::endl;
    success = false;
  }
  if (num_channels_ > fuchsia::media::MAX_PCM_CHANNEL_COUNT) {
    std::cerr << "Number of channels must be no greater than "
              << fuchsia::media::MAX_PCM_CHANNEL_COUNT << std::endl;
    success = false;
  }

  if (frame_rate_ < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) {
    std::cerr << "Frame rate must be at least " << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND
              << std::endl;
    success = false;
  }
  if (frame_rate_ > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND) {
    std::cerr << "Frame rate must be no greater than " << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND
              << std::endl;
    success = false;
  }

  if (options_.frames_per_packet > (options_.frames_per_payload_buffer / 2) &&
      options_.frames_per_packet != options_.frames_per_payload_buffer) {
    std::cerr << "Packet size cannot be larger than half the total payload space" << std::endl;
    success = false;
  }
  if (options_.frames_per_packet < frame_rate_ / 1000) {
    std::cerr << "Packet size must be 1 millisecond or more" << std::endl;
    success = false;
  }

  if (options_.stream_gain_db.has_value()) {
    options_.stream_gain_db =
        std::clamp<float>(options_.stream_gain_db.value(), fuchsia::media::audio::MUTED_GAIN_DB,
                          fuchsia::media::audio::MAX_GAIN_DB);
  }

  if (options_.usage_gain_db.has_value()) {
    options_.usage_gain_db = std::clamp<float>(options_.usage_gain_db.value(),
                                               fuchsia::media::audio::MUTED_GAIN_DB, kUnityGainDb);
  }
  if (options_.usage_volume.has_value()) {
    options_.usage_volume =
        std::clamp<float>(options_.usage_volume.value(), fuchsia::media::audio::MIN_VOLUME,
                          fuchsia::media::audio::MAX_VOLUME);
  }

  CLI_CHECK(success, "Exiting.");
}

// Acquire a GainControl if needed, configure the renderer, and set renderer event callbacks
void WavPlayer::ConfigureRenderer() {
  CLI_CHECK(audio_renderer_, "audio_renderer must not be null");

  if (!options_.ultrasound) {
    // For AudioCore's 'flexible' clock, call SetReferenceClock() with a NULL clock.
    if (options_.clock_type == ClockType::Flexible) {
      audio_renderer_->SetReferenceClock(zx::clock(ZX_HANDLE_INVALID));
    }

    // Set our render stream format
    if (options_.usage.has_value()) {
      audio_renderer_->SetUsage(options_.usage.value());
    }

    fuchsia::media::AudioStreamType format;
    format.sample_format = sample_format_;
    format.channels = num_channels_;
    format.frames_per_second = frame_rate_;

    audio_renderer_->SetPcmStreamType(format);
  }

  SetAudioRendererEvents();
}

// AudioCore interface is used to change the gain/volume of usages.
void WavPlayer::SetLoudnessLevels(sys::ComponentContext* app_context) {
  if (options_.usage_gain_db.has_value() || options_.usage_volume.has_value()) {
    fuchsia::media::AudioCorePtr audio_core;
    app_context->svc()->Connect(audio_core.NewRequest());

    if (options_.usage_gain_db.has_value()) {
      audio_core->SetRenderUsageGain(options_.usage.value_or(kDefaultUsage),
                                     options_.usage_gain_db.value());
    }

    if (options_.usage_volume.has_value()) {
      fuchsia::media::Usage usage;
      usage.set_render_usage(options_.usage.value_or(kDefaultUsage));
      audio_core->BindUsageVolumeControl(std::move(usage), usage_volume_control_.NewRequest());

      usage_volume_control_.set_error_handler([this](zx_status_t status) {
        CLI_CHECK(Shutdown(),
                  "Client connection to fuchsia.media.audio.VolumeControl failed: " << status);
      });

      usage_volume_control_->SetVolume(options_.usage_volume.value());
    }

    // ... now just let the instance of audio_core go out of scope.
  }

  if (options_.stream_mute.has_value() || options_.stream_gain_db.has_value()) {
    audio_renderer_->BindGainControl(gain_control_.NewRequest());
    gain_control_.set_error_handler([this](zx_status_t status) {
      CLI_CHECK(Shutdown(),
                "Client connection to fuchsia.media.audio.GainControl failed: " << status);
    });

    // Set stream gain and mute, if specified.
    if (options_.stream_mute.has_value()) {
      gain_control_->SetMute(options_.stream_mute.value());
    }
    if (options_.stream_gain_db.has_value()) {
      gain_control_->SetGain(options_.stream_gain_db.value());
    }
  }
}

// Based on the user-specified values for signal frequency and milliseconds per payload, calculate
// the other related coefficients needed for our mapped memory section, and for our series of
// payloads that reference that section.
//
// We share a memory section with our AudioRenderer, divided into equally-sized payloads (size
// specified by the user). For now, we trim the end of the memory section, rather than handle the
// occasional irregularly-sized packet.
// TODO(mpuryear): handle end-of-buffer wraparound; make it a true ring buffer.
void WavPlayer::SetupPayloadCoefficients() {
  uint32_t sample_size;
  switch (sample_format_) {
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      sample_size = sizeof(int32_t);
      break;
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      sample_size = sizeof(int16_t);
      break;
    case fuchsia::media::AudioSampleFormat::FLOAT:
      sample_size = sizeof(float);
      break;
    default:
      printf("Unknown AudioSampleFormat: %u\n", sample_format_);
      Shutdown();
      return;
  }

  // As mentioned above, for 24-bit audio we use 32-bit samples (low byte 0).
  frame_size_ = num_channels_ * sample_size;

  bytes_per_packet_ = options_.frames_per_packet * frame_size_;

  // From the specified payload buffer size, determine how many packets fit.
  auto bytes_per_payload_buffer = options_.frames_per_payload_buffer * frame_size_;
  packets_per_payload_buffer_ = bytes_per_payload_buffer / bytes_per_packet_;
}

void WavPlayer::DisplayConfigurationSettings() {
  if (options_.ultrasound) {
    printf("\nUltrasoundRenderer configured for %d-channel %s at %u Hz", num_channels_,
           SampleFormatToString(sample_format_), frame_rate_);
  } else {
    auto it =
        std::find_if(kRenderUsageOptions.cbegin(), kRenderUsageOptions.cend(),
                     [usage = options_.usage.value_or(kDefaultUsage)](auto usage_string_and_usage) {
                       return usage == usage_string_and_usage.second;
                     });
    CLI_CHECK(it != kRenderUsageOptions.cend(), "no RenderUsage found");
    auto usage_str = it->first;
    if (!options_.usage.has_value()) {
      usage_str = (std::string("default (") + usage_str + ")").c_str();
    }
    printf("\nAudioRenderer configured for %d-channel %s at %u Hz with the %s usage", num_channels_,
           SampleFormatToString(sample_format_), frame_rate_, usage_str);

    if (options_.stream_gain_db.has_value()) {
      printf(",\nsetting stream gain to %.3f dB", options_.stream_gain_db.value());
    }
    if (options_.stream_mute.has_value()) {
      printf(",\nafter explicitly %s this stream",
             options_.stream_mute.value() ? "muting" : "unmuting");
    }

    if (options_.usage_gain_db.has_value() || options_.usage_volume.has_value()) {
      printf(",\nafter setting ");
      if (options_.usage_gain_db.has_value()) {
        printf("%s gain to %.3f dB%s", usage_str, options_.usage_gain_db.value(),
               (options_.usage_volume.has_value() ? " and " : ""));
      }
      if (options_.usage_volume.has_value()) {
        printf("%s volume to %.1f", usage_str, options_.usage_volume.value());
      }
    }
  }

  printf(".\nThe file '%s' will be played", options_.file_name.c_str());

  printf(".\nThe stream's reference clock will be ");
  switch (options_.clock_type) {
    case ClockType::Default:
      printf("the default clock");
      break;
    case ClockType::Flexible:
      printf("the AudioCore-provided 'flexible' clock");
      break;
  }

  printf(
      ".\nThe renderer will transport data using %u non-timestamped buffer sections of %u frames",
      packets_per_payload_buffer_, options_.frames_per_packet);

  printf(",\nusing previous packet completions for flow control (contiguous mode).\n\n");
}

// Create-map a VMO for sharing audio cross-process. Send a non-writable handle to AudioRenderer.
void WavPlayer::CreateMemoryMapping() {
  zx::vmo payload_vmo;
  zx_status_t status = payload_buffer_.CreateAndMap(
      bytes_per_packet_ * packets_per_payload_buffer_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
      &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  CLI_CHECK(status == ZX_OK || Shutdown(), "VmoMapper:::CreateAndMap failed: " << status);

  audio_renderer_->AddPayloadBuffer(0, std::move(payload_vmo));
}

// Once we receive our clock, start playback if we've already received a MinLeadTime value
// indicating an actual audio output device is present. If an output device has NOT yet been
// detected and initialized, we wait for it -- calling Play() from OnMinLeadTimeChanged().
void WavPlayer::GetClockAndStart() {
  audio_renderer_->GetReferenceClock([this](zx::clock received_clock) {
    reference_clock_ = std::move(received_clock);

    if (options_.verbose) {
      audio::clock::GetAndDisplayClockDetails(reference_clock_);

      auto mono_now = zx::clock::get_monotonic();
      printf("- Received ref clock at %lu.  (%s sufficient min_lead_time)\n", mono_now.get(),
             (min_lead_time_ >= kRealDeviceMinLeadTime ? "Received" : "Awaiting"));
    }

    if (min_lead_time_ >= kRealDeviceMinLeadTime && !started_) {
      Play();
    }
  });
}

// Prime (pre-submit) an initial set of packets, then start playback.
void WavPlayer::Play() {
  if (stopping_) {
    // No packets to send, so we're done! Shutdown will unwind everything and exit our loop.
    Shutdown();
    return;
  }

  zx::time ref_now;
  auto status = reference_clock_.read(ref_now.get_address());
  CLI_CHECK(status == ZX_OK || Shutdown(), "zx::clock::read failed during init: " << status);

  // We "prime" the audio renderer by submitting an initial set of packets before starting playback.
  // We will subsequently send the rest one at a time, from the completion of a previous packet.
  // When priming, we send down only as many packets as concurrently fit into our payload buffer.
  for (uint32_t packet_num = 0; packet_num < packets_per_payload_buffer_; ++packet_num) {
    SendPacket();
  }

  status = reference_clock_.read(ref_now.get_address());
  CLI_CHECK(status == ZX_OK || Shutdown(), "zx::clock::read failed during Play(): " << status);

  zx::time requested_ref_start_time = zx::time(fuchsia::media::NO_TIMESTAMP);
  auto media_start_pts = fuchsia::media::NO_TIMESTAMP;

  if (options_.verbose) {
    auto mono_time_result = audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
    CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
    auto mono_now = mono_time_result.take_value();

    auto requested_ref_str = RefTimeStrFromZxTime(requested_ref_start_time);
    auto requested_media_str = RefTimeStrFromZxTime(zx::time{media_start_pts});
    auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
    auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

    printf("\nCalling Play (ref %s, media %s) at ref_now %s : mono_now %s\n",
           requested_ref_str.c_str(), requested_media_str.c_str(), ref_now_str.c_str(),
           mono_now_str.c_str());
  }

  auto play_completion_func = [this](int64_t actual_ref_start, int64_t actual_media_start) {
    if (options_.verbose) {
      zx::time ref_now;
      auto status = reference_clock_.read(ref_now.get_address());
      CLI_CHECK(status == ZX_OK || Shutdown(),
                "zx::clock::read failed during Play callback: " << status);

      auto mono_time_result =
          audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
      CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
      auto mono_now = mono_time_result.take_value();

      auto actual_ref_str = RefTimeStrFromZxTime(zx::time{actual_ref_start});
      auto actual_media_str = RefTimeStrFromZxTime(zx::time{actual_media_start});
      auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
      auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

      printf("Play callback(ref %s, media %s) at ref_now %s : mono_now %s\n\n",
             actual_ref_str.c_str(), actual_media_str.c_str(), ref_now_str.c_str(),
             mono_now_str.c_str());
    }
  };

  audio_renderer_->Play(requested_ref_start_time.get(), media_start_pts, play_completion_func);
  started_ = true;
}

bool WavPlayer::CheckPayloadSpace() {
  if (num_packets_completed_ > 0 && num_packets_sent_ <= num_packets_completed_) {
    printf("! Sending: packet %4lu; packet %4lu has already completed - did we underrun?\n",
           num_packets_sent_, num_packets_completed_);
    return false;
  }

  if (num_packets_sent_ >= num_packets_completed_ + packets_per_payload_buffer_) {
    printf("! Sending: packet %4lu; only %4lu have completed - did we overrun?\n",
           num_packets_sent_, num_packets_completed_);
    return false;
  }

  return true;
}

// Our buffer is backed by a VMO and sub-divided into uniformly-sized zones, called payloads.
// Thus this buffer that we share with the AudioRenderer is called the payload buffer.
//
// Each packet indicates the next payload of data. This wraps around to the start of the buffer,
// once we reach its end. For example, with a buffer that can hold 2 payloads, we would send
// audio packets in the following order:
//
//  ------------------------
// | buffer_id | payload_id |
// |   (vmo)   |  (offset)  |
// |-----------|------------|
// | buffer 0  |  payload 0 |
// | buffer 0  |  payload 1 |
// | buffer 0  |  payload 0 |
// |      ... etc ...       |
//  ------------------------
fuchsia::media::StreamPacket WavPlayer::CreateAudioPacket(uint64_t packet_num) {
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = (packet_num % packets_per_payload_buffer_) * bytes_per_packet_;
  packet.payload_size = bytes_per_packet_;

  return packet;
}

uint64_t WavPlayer::RetrieveAudioForPacket(const fuchsia::media::StreamPacket& packet,
                                           uint64_t packet_num) {
  if (looping_reached_end_of_file_) {
    auto reset_status = wav_reader_->Reset();
    CLI_CHECK_OK(reset_status, "Could not reset file read pointer to beginning of file");

    looping_reached_end_of_file_ = false;
  }

  auto audio_buff = reinterpret_cast<uint8_t*>(payload_buffer_.start()) + packet.payload_offset;
  uint64_t bytes_added = 0;

  // WavReader copies audio samples from the WAV file directly into our payload buffer.
  auto status = wav_reader_->Read(audio_buff, packet.payload_size);
  CLI_CHECK(status.is_ok(), "Error from wav_reader::Read: " + std::to_string(status.error()));

  // We track how much data was written into our outgoing audio buffer
  bytes_added = status.value();

  if (bytes_added < packet.payload_size) {
    if (bytes_added == 0) {
      if (options_.loop_playback) {
        looping_reached_end_of_file_ = true;
        bytes_added = RetrieveAudioForPacket(packet, packet_num);
      } else {
        stopping_ = true;
      }
    }

    // Extra-safe but unnecessary, since we shorten the final packet based on bytes_added retval.
    std::memset(&audio_buff[bytes_added], 0, packet.payload_size - bytes_added);
  }

  return bytes_added;
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if there are more packets to send, create and send the next packet;
// b. if all expected packets have completed, begin closing down the system.
void WavPlayer::SendPacket() {
  // If we reached end-of-file (not looping) or got a keypress, no need to send more packets.
  if (stopping_) {
    return;
  }

  CLI_CHECK(CheckPayloadSpace(), "Insufficient payload buffer space -- synchronization issue?");

  auto packet = CreateAudioPacket(num_packets_sent_);

  auto actual_bytes_retrieved = RetrieveAudioForPacket(packet, num_packets_sent_);
  // RetrieveAudioForPacket will never return MORE data than expected
  CLI_CHECK(actual_bytes_retrieved <= bytes_per_packet_, "RetrieveAudioForPacket size too large");

  // If actual_bytes_retrieved is less than bytes_per_packet_, this is the last packet at EOF.
  // We might be looping, so we let RetrieveAudioForPacket handle whether to set stopping_.
  // Either way, we should play out this last partially-filled packet, unless it is truly empty --
  // in which case we can safely return without doing anything.
  if (!actual_bytes_retrieved) {
    return;
  }
  if (actual_bytes_retrieved < bytes_per_packet_) {
    packet.payload_size = actual_bytes_retrieved;
  }

  if (options_.verbose) {
    zx::time ref_now;
    auto status = reference_clock_.read(ref_now.get_address());
    CLI_CHECK((status == ZX_OK) || Shutdown(),
              "zx::clock::read failed during SendPacket(): " << status);

    auto mono_time_result = audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
    CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
    auto mono_now = mono_time_result.take_value();

    auto pts_str = RefTimeStrFromZxTime(zx::time{packet.pts});
    auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
    auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

    printf("  Sending: packet %4lu (media pts %s) :  ref_now %s :  mono_now %s\n",
           num_packets_sent_, pts_str.c_str(), ref_now_str.c_str(), mono_now_str.c_str());
  }

  ++num_packets_sent_;
  uint64_t frames_completed = packet.payload_size / frame_size_;
  audio_renderer_->SendPacket(
      packet, [this, frames_completed]() { OnSendPacketComplete(frames_completed); });
}

void WavPlayer::OnSendPacketComplete(uint64_t frames_completed) {
  num_frames_completed_ += frames_completed;

  if (options_.verbose) {
    zx::time ref_now;
    auto status = reference_clock_.read(ref_now.get_address());
    CLI_CHECK(status == ZX_OK || Shutdown(),
              "zx::clock::read failed during OnSendPacketComplete(): " << status);

    auto mono_time_result = audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
    CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
    auto mono_now = mono_time_result.take_value();

    auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
    auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

    printf("Completed: packet %4lu (%5lu frames, up to %8lu ) :  ref_now %s :  mono_now %s\n",
           num_packets_completed_, frames_completed, num_frames_completed_, ref_now_str.c_str(),
           mono_now_str.c_str());
  }

  ++num_packets_completed_;
  CLI_CHECK(num_packets_completed_ <= num_packets_sent_,
            "packets_completed cannot exceed num_packets_sent !");

  if (num_packets_completed_ == num_packets_sent_) {
    Shutdown();
  } else if (!stopping_) {
    SendPacket();
  }
}

// Enable audio renderer callbacks
void WavPlayer::SetAudioRendererEvents() {
  audio_renderer_.events().OnMinLeadTimeChanged = [this](int64_t min_lead_time_nsec) {
    min_lead_time_ = zx::duration(min_lead_time_nsec);

    if (options_.verbose) {
      printf("- OnMinLeadTimeChanged: %lu at %lu: %s to start playback  (%s ref clock)\n",
             min_lead_time_nsec, zx::clock::get_monotonic().get(),
             (min_lead_time_ >= kRealDeviceMinLeadTime ? "sufficient" : "insufficient"),
             (reference_clock_.is_valid() ? "Received" : "Awaiting"));
    }

    if (min_lead_time_ >= kRealDeviceMinLeadTime && reference_clock_.is_valid() && !started_) {
      Play();
    }
  };

  audio_renderer_->EnableMinLeadTimeEvents(true);
}

// When a key is pressed, don't send additional packets. Also, recall existing packets (don't wait
// for the multi-sec buffer to empty out).
void WavPlayer::OnKeyPress() {
  stopping_ = true;
  audio_renderer_->DiscardAllPacketsNoReply();
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon ~WavPlayer).
bool WavPlayer::Shutdown() {
  stopping_ = true;

  gain_control_.Unbind();
  usage_volume_control_.Unbind();
  audio_renderer_.Unbind();

  payload_buffer_.Unmap();
  options_.quit_callback();

  return false;
}

}  // namespace media::tools
