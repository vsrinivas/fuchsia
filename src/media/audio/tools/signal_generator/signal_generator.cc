// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/tools/signal_generator/signal_generator.h"

#include <zircon/syscalls.h>

#include <iostream>

#include <fbl/algorithm.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::tools {

constexpr auto kPlayStartupDelay = zx::msec(0);

std::string RefTimeStrFromZxTime(zx::time zx_time) {
  auto time = zx_time.get();

  if (time == fuchsia::media::NO_TIMESTAMP) {
    return "  [NO_TIMESTAMP]   ";
  }

  char time_chars[20];
  sprintf(time_chars, "%07lu'%03lu'%03lu'%03lu", time / ZX_SEC(1), (time % ZX_SEC(1)) / ZX_MSEC(1),
          (time % ZX_MSEC(1)) / ZX_USEC(1), time % ZX_USEC(1));
  time_chars[19] = 0;

  return std::string(time_chars);
}

std::string RefTimeMsStrFromZxTime(zx::time zx_time) {
  auto time = zx_time.get();

  if (time == fuchsia::media::NO_TIMESTAMP) {
    return "  [NO_TIMESTAMP]       ";
  }

  char time_chars[24];
  sprintf(time_chars, "%07lu'%03lu.%02lu millisec", time / ZX_SEC(1),
          (time % ZX_SEC(1)) / ZX_MSEC(1), (time % ZX_MSEC(1)) / ZX_USEC(10));
  time_chars[23] = 0;

  return std::string(time_chars);
}

MediaApp::MediaApp(fit::closure quit_callback) : quit_callback_(std::move(quit_callback)) {
  FX_DCHECK(quit_callback_);
}

// Prepare for playback, submit initial data, start the presentation timeline.
void MediaApp::Run(sys::ComponentContext* app_context) {
  // Check the cmdline flags; exit if any are invalid or out-of-range.
  if (!ParameterRangeChecks()) {
    Shutdown();
    return;
  }

  // Calculate the frame size, number of packets, and shared-buffer size.
  SetupPayloadCoefficients();

  // Show a summary of all our settings: exactly what we are about to do.
  DisplayConfigurationSettings();

  SetAudioCoreSettings(app_context);
  AcquireAudioRenderer(app_context);

  // Set our render stream format, plus other settings as needed: gain, clock, continuity threshold
  if (ConfigureAudioRenderer() != ZX_OK) {
    Shutdown();
    return;
  }

  // If requested, configure a WavWriter that will concurrently write this signal to a WAV file.
  if (!InitializeWavWriter()) {
    Shutdown();
    return;
  }

  // Create VmoMapper(s) that Create+Map a VMO. Send these down via AudioRenderer::AddPayloadBuffer.
  if (CreateMemoryMapping() != ZX_OK) {
    Shutdown();
    return;
  }

  // Start playback for this renderer.
  Play();
}

bool MediaApp::ParameterRangeChecks() {
  bool ret_val = true;

  if (num_channels_ < fuchsia::media::MIN_PCM_CHANNEL_COUNT) {
    std::cerr << "Number of channels must be at least " << fuchsia::media::MIN_PCM_CHANNEL_COUNT
              << std::endl;
    ret_val = false;
  }
  if (num_channels_ > fuchsia::media::MAX_PCM_CHANNEL_COUNT) {
    std::cerr << "Number of channels must be no greater than "
              << fuchsia::media::MAX_PCM_CHANNEL_COUNT << std::endl;
    ret_val = false;
  }

  if (frame_rate_ < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) {
    std::cerr << "Frame rate must be at least " << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND
              << std::endl;
    ret_val = false;
  }
  if (frame_rate_ > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND) {
    std::cerr << "Frame rate must be no greater than " << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND
              << std::endl;
    ret_val = false;
  }

  if (frequency_ < 0.0) {
    std::cerr << "Frequency cannot be negative" << std::endl;
    ret_val = false;
  }

  if (amplitude_ > 1.0) {
    std::cerr << "Amplitude must be no greater than 1.0" << std::endl;
    ret_val = false;
  }
  if (amplitude_ < -1.0) {
    std::cerr << "Amplitude must be no less than -1.0" << std::endl;
    ret_val = false;
  }

  if (duration_secs_ < 0.0) {
    std::cerr << "Duration cannot be negative" << std::endl;
    ret_val = false;
  }

  if (frames_per_payload_ > frame_rate_ / 2) {
    std::cerr << "Payload size must be 500 milliseconds or less" << std::endl;
    ret_val = false;
  }
  if (frames_per_payload_ < frame_rate_ / 1000) {
    std::cerr << "Payload size must be 1 millisecond or more" << std::endl;
    ret_val = false;
  }

  stream_gain_db_ = fbl::clamp<float>(stream_gain_db_, fuchsia::media::audio::MUTED_GAIN_DB,
                                      fuchsia::media::audio::MAX_GAIN_DB);

  usage_gain_db_ =
      fbl::clamp<float>(usage_gain_db_, fuchsia::media::audio::MUTED_GAIN_DB, kUnityGainDb);
  usage_volume_ = fbl::clamp<float>(usage_volume_, fuchsia::media::audio::MIN_VOLUME,
                                    fuchsia::media::audio::MAX_VOLUME);

  return ret_val;
}

// Based on the user-specified values for signal frequency and milliseconds per payload, calculate
// the other related coefficients needed for our mapped memory section, and for our series of
// payloads that reference that section.
//
// We share a memory section with our AudioRenderer, divided into equally-sized payloads (size
// specified by the user). For now, we trim the end of the memory section, rather than handle the
// occasional irregularly-sized packet.
// TODO(mpuryear): handle end-of-buffer wraparound; make it a true ring buffer.
void MediaApp::SetupPayloadCoefficients() {
  total_frames_to_send_ = duration_secs_ * frame_rate_;
  num_packets_to_send_ = total_frames_to_send_ / frames_per_payload_;
  if (num_packets_to_send_ * frames_per_payload_ < total_frames_to_send_) {
    ++num_packets_to_send_;
  }

  // Number of frames in each period of the recurring signal.
  frames_per_period_ = frame_rate_ / frequency_;

  amplitude_scalar_ = amplitude_;
  if (use_int24_) {
    amplitude_scalar_ *= (std::numeric_limits<int32_t>::max() & 0xFFFFFF00);
  } else if (use_int16_) {
    amplitude_scalar_ *= std::numeric_limits<int16_t>::max();
  }

  // As mentioned above, for 24-bit audio we use 32-bit samples (low byte 0).
  sample_size_ = use_int24_ ? sizeof(int32_t) : (use_int16_ ? sizeof(int16_t) : sizeof(float));
  frame_size_ = num_channels_ * sample_size_;

  payload_size_ = frames_per_payload_ * frame_size_;

  // First, assume one second of audio, determine how many payloads will fit, then trim the mapping
  // to the amount that will be used. This mapping size will be split across |num_payload_buffers_|
  // buffers. For example, with 2 buffers each will be large enough hold 500ms of data.
  auto total_mapping_size = frame_rate_ * frame_size_;
  total_num_mapped_payloads_ = total_mapping_size / payload_size_;

  // Shard out the payloads across multiple buffers, ensuring we can hold at least 1 buffer.
  payloads_per_mapping_ = std::max(1u, total_num_mapped_payloads_ / num_payload_buffers_);
  payload_mapping_size_ = payloads_per_mapping_ * payload_size_;
}

void MediaApp::DisplayConfigurationSettings() {
  auto it = std::find_if(kRenderUsageOptions.cbegin(), kRenderUsageOptions.cend(),
                         [usage = usage_](auto usage_string_and_usage) {
                           return usage == usage_string_and_usage.second;
                         });
  FX_DCHECK(it != kRenderUsageOptions.cend());
  auto usage_str = it->first;

  printf("\nAudioRenderer configured for %d-channel %s at %u Hz with the %s usage.", num_channels_,
         (use_int24_ ? "int24" : (use_int16_ ? "int16" : "float32")), frame_rate_, usage_str);

  printf("\nContent is ");
  if (output_signal_type_ == kOutputTypeNoise) {
    printf("white noise");
  } else {
    printf("a %f Hz %s wave", frequency_,
           (output_signal_type_ == kOutputTypeSquare)
               ? "square"
               : (output_signal_type_ == kOutputTypeSawtooth) ? "triangle" : "sine");
  }

  printf(", amplitude %f", amplitude_);
  if (ramp_stream_gain_) {
    printf(",\nramping stream gain from %.3f dB to %.3f dB over %.6lf seconds (%ld nanoseconds)",
           stream_gain_db_, ramp_target_gain_db_,
           static_cast<double>(ramp_duration_nsec_) / 1000000000, ramp_duration_nsec_);
  } else if (set_stream_gain_) {
    printf(", at stream gain %.3f dB", stream_gain_db_);
  }
  if (set_stream_mute_) {
    printf(", after explicitly %s this stream", stream_mute_ ? "muting" : "unmuting");
  }

  printf(".\nSignal will play for %.3f seconds, using %u %stimestamped buffers of %u frames",
         duration_secs_, total_num_mapped_payloads_, (!use_pts_ ? "non-" : ""),
         frames_per_payload_);

  if (set_continuity_threshold_) {
    printf(",\nhaving set the PTS continuity threshold to %f seconds",
           pts_continuity_threshold_secs_);
  }

  if (set_usage_gain_ || set_usage_volume_) {
    printf(",\nafter setting ");
    if (set_usage_gain_) {
      printf("%s gain to %.3f dB%s", usage_str, usage_gain_db_, (set_usage_volume_ ? " and " : ""));
    }
    if (set_usage_volume_) {
      printf("%s volume to %.1f", usage_str, usage_volume_);
    }
  }

  printf(".\n\n");
}

// AudioCore interface is used to change the gain/volume of usages.
void MediaApp::SetAudioCoreSettings(sys::ComponentContext* app_context) {
  if (set_usage_gain_ || set_usage_volume_) {
    fuchsia::media::AudioCorePtr audio_core;
    app_context->svc()->Connect(audio_core.NewRequest());

    if (set_usage_gain_) {
      audio_core->SetRenderUsageGain(usage_, usage_gain_db_);
    }

    if (set_usage_volume_) {
      fuchsia::media::Usage usage;
      usage.set_render_usage(usage_);
      audio_core->BindUsageVolumeControl(std::move(usage), usage_volume_control_.NewRequest());

      usage_volume_control_.set_error_handler([this](zx_status_t status) {
        std::cerr << "Client connection to fuchsia.media.audio.VolumeControl failed: " << status
                  << std::endl;
        Shutdown();
      });
    }

    // ... now just let the instance of audio_core go out of scope.
  }
}

// Use ComponentContext to acquire AudioPtr; use that to acquire AudioRendererPtr in turn. Set
// AudioRenderer error handler, in case of channel closure.
void MediaApp::AcquireAudioRenderer(sys::ComponentContext* app_context) {
  // Audio interface is needed to create AudioRenderer and set routing policy.
  fuchsia::media::AudioPtr audio;
  app_context->svc()->Connect(audio.NewRequest());

  audio->CreateAudioRenderer(audio_renderer_.NewRequest());

  SetAudioRendererEvents();

  audio_renderer_->BindGainControl(gain_control_.NewRequest());
  gain_control_.set_error_handler([this](zx_status_t status) {
    std::cerr << "Client connection to fuchsia.media.audio.GainControl failed: " << status
              << std::endl;
    Shutdown();
  });

  // ... now just let the instance of audio go out of scope.
  //
  // Although we could technically call gain_control_'s SetMute|SetGain|SetGainWithRamp here and
  // then disconnect it (like we do for audio_core and audio), we instead maintain our GainControl
  // throughout playback, just in case we someday want to change gain during playback.
}

// Set the AudioRenderer's audio format, plus other settings requested by command line
zx_status_t MediaApp::ConfigureAudioRenderer() {
  FX_DCHECK(audio_renderer_);

  zx_status_t status = ZX_OK;
  fuchsia::media::AudioStreamType format;

  format.sample_format = (use_int24_ ? fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32
                                     : (use_int16_ ? fuchsia::media::AudioSampleFormat::SIGNED_16
                                                   : fuchsia::media::AudioSampleFormat::FLOAT));
  format.channels = num_channels_;
  format.frames_per_second = frame_rate_;

  if (use_pts_) {
    audio_renderer_->SetPtsUnits(frame_rate_, 1);
  }
  if (set_continuity_threshold_) {
    audio_renderer_->SetPtsContinuityThreshold(pts_continuity_threshold_secs_);
  }

  audio_renderer_->SetUsage(usage_);

  audio_renderer_->SetPcmStreamType(format);

  // Set usage volume, if specified.
  if (set_usage_volume_) {
    usage_volume_control_->SetVolume(usage_volume_);
  }

  // Set stream gain and mute, if specified.
  if (set_stream_mute_) {
    gain_control_->SetMute(stream_mute_);
  }
  if (set_stream_gain_) {
    gain_control_->SetGain(stream_gain_db_);
  }
  if (ramp_stream_gain_) {
    gain_control_->SetGainWithRamp(ramp_target_gain_db_, ramp_duration_nsec_,
                                   fuchsia::media::audio::RampType::SCALE_LINEAR);
  }

  return status;
}

bool MediaApp::InitializeWavWriter() {
  // 24-bit buffers use 32-bit samples (lowest byte zero), and when this particular utility saves
  // to .wav file, we save the entire 32 bits.
  if (save_to_file_) {
    if (!wav_writer_.Initialize(file_name_.c_str(),
                                use_int24_
                                    ? fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32
                                    : (use_int16_ ? fuchsia::media::AudioSampleFormat::SIGNED_16
                                                  : fuchsia::media::AudioSampleFormat::FLOAT),
                                num_channels_, frame_rate_, sample_size_ * 8)) {
      std::cerr << "WavWriter::Initialize() failed" << std::endl;
      return false;
    }
    wav_writer_initialized_ = true;
  }
  return true;
}

// Create a VMO and map memory for 1 sec of audio between them. Reduce rights and send handle to
// AudioRenderer: this is our shared buffer.
zx_status_t MediaApp::CreateMemoryMapping() {
  for (size_t i = 0; i < num_payload_buffers_; ++i) {
    auto& payload_buffer = payload_buffers_.emplace_back();
    zx::vmo payload_vmo;
    zx_status_t status = payload_buffer.CreateAndMap(
        payload_mapping_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &payload_vmo,
        ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

    if (status != ZX_OK) {
      std::cerr << "VmoMapper:::CreateAndMap failed: " << status << std::endl;
      return status;
    }

    audio_renderer_->AddPayloadBuffer(i, std::move(payload_vmo));
  }

  return ZX_OK;
}

// Prime (pre-submit) an initial set of packets, then start playback.
void MediaApp::Play() {
  if (num_packets_to_send_ > 0) {
    // We can only send down as many packets as will concurrently fit into our payload buffer.
    // The rest will be sent, one at a time, from a previous packet's completion callback.
    uint32_t num_packets_to_prime =
        fbl::min<uint64_t>(total_num_mapped_payloads_, num_packets_to_send_);
    for (uint32_t packet_num = 0; packet_num < num_packets_to_prime; ++packet_num) {
      SendPacket();
    }

    auto now = zx::clock::get_monotonic();
    auto now_str = RefTimeMsStrFromZxTime(now);

    reference_start_time_ = use_pts_ ? (now + zx::duration{min_lead_time_} + kPlayStartupDelay)
                                     : zx::time{fuchsia::media::NO_TIMESTAMP};
    media_start_time_ = zx::time{use_pts_ ? 0 : fuchsia::media::NO_TIMESTAMP};

    auto requested_ref_str = RefTimeStrFromZxTime(reference_start_time_);
    auto requested_media_str = RefTimeStrFromZxTime(media_start_time_);

    if (verbose_) {
      printf("\nCalling Play (ref %s, media %s) at %s\n", requested_ref_str.c_str(),
             requested_media_str.c_str(), now_str.c_str());
    }

    auto play_completion_func = [this](int64_t actual_ref_start, int64_t actual_media_start) {
      if (verbose_) {
        auto now = zx::clock::get_monotonic();
        auto now_str = RefTimeMsStrFromZxTime(now);

        auto actual_ref_str = RefTimeStrFromZxTime(zx::time{actual_ref_start});
        auto actual_media_str = RefTimeStrFromZxTime(zx::time{actual_media_start});

        printf("Play callback(ref %s, media %s) at %s\n\n", actual_ref_str.c_str(),
               actual_media_str.c_str(), now_str.c_str());
      }
    };

    audio_renderer_->Play(reference_start_time_.get(), media_start_time_.get(),
                          play_completion_func);
  } else {
    // No packets to send, so we're done! Shutdown will unwind everything and exit our loop.
    Shutdown();
  }
}

// We have a set of buffers each backed by its own VMO, with each buffer sub-divided into
// uniformly-sized zones, called payloads.
//
// We round robin packets across each buffer, wrapping around to the start of each buffer once the
// end is encountered. For example, with 2 buffers that can each hold 2 payloads, we would send
// audio packets in the following order:
//
//  ------------------------
// | buffer_id | payload_id |
// |   (vmo)   |  (offset)  |
// |-----------|------------|
// | buffer 0  |  payload 0 |
// | buffer 1  |  payload 0 |
// | buffer 0  |  payload 1 |
// | buffer 1  |  payload 1 |
// | buffer 0  |  payload 0 |
// |      ... etc ...       |
//  ------------------------
MediaApp::AudioPacket MediaApp::CreateAudioPacket(uint64_t packet_num) {
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = packet_num % num_payload_buffers_;

  auto buffer_payload_index = packet_num / num_payload_buffers_;
  packet.payload_offset = (buffer_payload_index % payloads_per_mapping_) * payload_size_;

  // If last payload, send exactly what remains (otherwise send a full payload).
  packet.payload_size =
      (packet_num + 1 == num_packets_to_send_)
          ? (total_frames_to_send_ - (packet_num * frames_per_payload_)) * frame_size_
          : payload_size_;

  // packet.pts (media time) is NO_TIMESTAMP by default unless we override it.
  if (use_pts_) {
    packet.pts = packet_num * frames_per_payload_;
  }

  return {
      .stream_packet = std::move(packet),
      .vmo = &payload_buffers_[packet.payload_buffer_id],
  };
}

void MediaApp::GenerateAudioForPacket(const AudioPacket& audio_packet, uint64_t packet_num) {
  const auto& packet = audio_packet.stream_packet;
  auto audio_buff = reinterpret_cast<uint8_t*>(audio_packet.vmo->start()) + packet.payload_offset;

  // Recompute payload_frames each time, since the final packet may be 'short'.
  //
  // TODO(mpuryear): don't recompute this every time; use payload_frames_ (and pre-compute this)
  // except for last packet, which we either check for here or pass in as a boolean parameter.
  uint32_t payload_frames = packet.payload_size / frame_size_;

  if (use_int24_) {
    WriteAudioIntoBuffer<int32_t>(reinterpret_cast<int32_t*>(audio_buff), payload_frames,
                                  frames_per_payload_ * packet_num, output_signal_type_,
                                  num_channels_, frames_per_period_, amplitude_scalar_);
  } else if (use_int16_) {
    WriteAudioIntoBuffer<int16_t>(reinterpret_cast<int16_t*>(audio_buff), payload_frames,
                                  frames_per_payload_ * packet_num, output_signal_type_,
                                  num_channels_, frames_per_period_, amplitude_scalar_);
  } else {
    WriteAudioIntoBuffer<float>(reinterpret_cast<float*>(audio_buff), payload_frames,
                                frames_per_payload_ * packet_num, output_signal_type_,
                                num_channels_, frames_per_period_, amplitude_scalar_);
  }
}

// Write signal into the next section of our buffer. Track how many total frames since playback
// started, to handle arbitrary frequencies of type double.
template <typename SampleType>
void MediaApp::WriteAudioIntoBuffer(SampleType* audio_buffer, uint32_t num_frames,
                                    uint64_t frames_since_start, OutputSignalType signal_type,
                                    uint32_t num_chans, double frames_per_period,
                                    double amp_scalar) {
  double raw_val;  // Generated signal val, before applying amplitude scaling.
  double rads_per_frame = 2.0 * M_PI / frames_per_period;  // Radians/Frame.

  for (uint32_t frame = 0; frame < num_frames; ++frame, ++frames_since_start) {
    switch (signal_type) {
      case kOutputTypeSine:
        raw_val = sin(rads_per_frame * frames_since_start);
        break;
      case kOutputTypeSquare:
        raw_val =
            (fmod(frames_since_start, frames_per_period) >= frames_per_period / 2) ? -1.0 : 1.0;
        break;
      case kOutputTypeSawtooth:
        raw_val = (fmod(frames_since_start / frames_per_period, 1.0) * 2.0) - 1.0;
        break;
      case kOutputTypeNoise:
        // TODO(mpuryear): consider making the white noise generator even more truly random, with
        // multiple rand() calls at different frequencies.
        raw_val = static_cast<double>(rand()) / RAND_MAX * 2.0 - 1.0;
        break;
    }

    SampleType val = raw_val * amp_scalar;

    // If generating a 24-in-32 signal, clear the unused bottom 8 bits.
    if (std::is_same_v<SampleType, int32_t>) {
      val = static_cast<int32_t>(val) & 0xFFFFFF00;
    }

    // Put the same content into all channels (even white noise)
    // TODO(mpuryear): for white noise, treat each channel independently.
    for (uint32_t chan_num = 0; chan_num < num_chans; ++chan_num) {
      audio_buffer[frame * num_chans + chan_num] = val;
    }
  }
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if there are more packets to send, create and send the next packet;
// b. if all expected packets have completed, begin closing down the system.
void MediaApp::SendPacket() {
  auto packet = CreateAudioPacket(num_packets_sent_);

  GenerateAudioForPacket(packet, num_packets_sent_);

  if (save_to_file_) {
    if (!wav_writer_.Write(
            reinterpret_cast<char*>(packet.vmo->start()) + packet.stream_packet.payload_offset,
            packet.stream_packet.payload_size)) {
      std::cerr << "WavWriter::Write() failed" << std::endl;
    }
  }

  if (verbose_) {
    auto now = zx::clock::get_monotonic();

    auto pts_str = RefTimeStrFromZxTime(zx::time{packet.stream_packet.pts});
    auto now_str = RefTimeMsStrFromZxTime(now);

    printf("Sending packet %4lu (media pts %s) at %s\n", num_packets_sent_, pts_str.c_str(),
           now_str.c_str());
  }

  ++num_packets_sent_;
  num_frames_sent_ += packet.stream_packet.payload_size / frame_size_;
  uint64_t frames_completed = packet.stream_packet.payload_size / frame_size_;
  audio_renderer_->SendPacket(
      packet.stream_packet, [this, frames_completed]() { OnSendPacketComplete(frames_completed); });
}

void MediaApp::OnSendPacketComplete(uint64_t frames_completed) {
  num_frames_completed_ += frames_completed;

  if (verbose_) {
    auto now = zx::clock::get_monotonic();

    auto now_str = RefTimeMsStrFromZxTime(now);
    printf("Packet %4lu complete (%5lu, %8lu frames total) at %s\n", num_packets_completed_,
           frames_completed, num_frames_completed_, now_str.c_str());
  }

  ++num_packets_completed_;
  FX_DCHECK(num_packets_completed_ <= num_packets_to_send_);

  if (num_packets_sent_ < num_packets_to_send_) {
    SendPacket();
  } else if (num_packets_completed_ >= num_packets_to_send_) {
    Shutdown();
  }
}

// Enable audio renderer callbacks
void MediaApp::SetAudioRendererEvents() {
  audio_renderer_.set_error_handler([this](zx_status_t status) {
    std::cerr << "Client connection to fuchsia.media.AudioRenderer failed: " << status << std::endl;
    Shutdown();
  });

  audio_renderer_.events().OnMinLeadTimeChanged = [this](int64_t min_lead_time_nsec) {
    if (verbose_) {
      printf("- OnMinLeadTimeChanged: %lu\n", min_lead_time_nsec);
    }
    received_min_lead_time_ = true;
    min_lead_time_ = min_lead_time_nsec;
  };

  audio_renderer_->EnableMinLeadTimeEvents(true);
}

// Disable audio renderer callbacks
void MediaApp::ResetAudioRendererEvents() {
  if (audio_renderer_.is_bound()) {
    audio_renderer_->EnableMinLeadTimeEvents(false);
    audio_renderer_.events().OnMinLeadTimeChanged = nullptr;
    audio_renderer_.set_error_handler(nullptr);
  }
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon ~MediaApp).
void MediaApp::Shutdown() {
  ResetAudioRendererEvents();

  if (wav_writer_initialized_) {
    if (!wav_writer_.Close()) {
      std::cerr << "WavWriter::Close() failed" << std::endl;
    }
  }

  payload_buffers_.clear();
  quit_callback_();
}

}  // namespace media::tools
