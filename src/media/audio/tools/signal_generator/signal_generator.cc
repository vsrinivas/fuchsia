// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/tools/signal_generator/signal_generator.h"

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <math.h>
#include <zircon/syscalls.h>

#include <fbl/algorithm.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::tools {

MediaApp::MediaApp(fit::closure quit_callback) : quit_callback_(std::move(quit_callback)) {
  FX_DCHECK(quit_callback_);
}

// Prepare for playback, submit initial data, start the presentation timeline.
void MediaApp::Run(sys::ComponentContext* app_context) {
  if (!ParameterRangeChecks()) {
    Shutdown();
    return;
  }

  SetupPayloadCoefficients();
  DisplayConfigurationSettings();
  SetAudioCoreSettings(app_context);
  AcquireAudioRenderer(app_context);
  SetStreamType();

  if (CreateMemoryMapping() != ZX_OK) {
    Shutdown();
    return;
  }

  // 24-bit buffers use 32-bit samples (lowest byte zero), and when this particular utility saves to
  // .wav file, we save the entire 32 bits.
  if (save_to_file_) {
    if (!wav_writer_.Initialize(file_name_.c_str(),
                                use_int24_
                                    ? fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32
                                    : (use_int16_ ? fuchsia::media::AudioSampleFormat::SIGNED_16
                                                  : fuchsia::media::AudioSampleFormat::FLOAT),
                                num_channels_, frame_rate_, sample_size_ * 8)) {
      FX_LOGS(ERROR) << "WavWriter::Initialize() failed";
    } else {
      wav_writer_is_initialized_ = true;
    }
  }

  if (num_packets_to_send_ > 0) {
    uint32_t num_packets_to_prime =
        fbl::min<uint64_t>(total_num_mapped_payloads_, num_packets_to_send_);
    for (uint32_t packet_num = 0; packet_num < num_packets_to_prime; ++packet_num) {
      SendPacket(packet_num);
    }

    audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                                 (use_pts_ ? 0 : fuchsia::media::NO_TIMESTAMP));
  } else {
    Shutdown();
  }
}

bool MediaApp::ParameterRangeChecks() {
  bool ret_val = true;

  if (num_channels_ < fuchsia::media::MIN_PCM_CHANNEL_COUNT) {
    FX_LOGS(ERROR) << "Number of channels must be at least "
                   << fuchsia::media::MIN_PCM_CHANNEL_COUNT;
    ret_val = false;
  }
  if (num_channels_ > fuchsia::media::MAX_PCM_CHANNEL_COUNT) {
    FX_LOGS(ERROR) << "Number of channels must be no greater than "
                   << fuchsia::media::MAX_PCM_CHANNEL_COUNT;
    ret_val = false;
  }

  if (frame_rate_ < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) {
    FX_LOGS(ERROR) << "Frame rate must be at least " << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND;
    ret_val = false;
  }
  if (frame_rate_ > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND) {
    FX_LOGS(ERROR) << "Frame rate must be no greater than "
                   << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND;
    ret_val = false;
  }

  if (frequency_ < 0.0) {
    FX_LOGS(ERROR) << "Frequency cannot be negative";
    ret_val = false;
  }

  if (amplitude_ > 1.0) {
    FX_LOGS(ERROR) << "Amplitude must be no greater than 1.0";
    ret_val = false;
  }
  if (amplitude_ < -1.0) {
    FX_LOGS(ERROR) << "Amplitude must be no less than -1.0";
    ret_val = false;
  }

  if (duration_secs_ < 0.0) {
    FX_LOGS(ERROR) << "Duration cannot be negative";
    ret_val = false;
  }

  if (frames_per_payload_ > frame_rate_ / 2) {
    FX_LOGS(ERROR) << "Payload size must be 500 milliseconds or less.";
    ret_val = false;
  }
  if (frames_per_payload_ < frame_rate_ / 1000) {
    FX_LOGS(ERROR) << "Payload size must be 1 millisecond or more.";
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
  if (set_device_settings_) {
    printf("\nSetting device settings to %s.", (settings_enabled_ ? "ON" : "OFF"));
  }

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
    printf(", after explicitly %smuting this stream", stream_mute_ ? "" : "un");
  }

  printf(
      ".\nSignal will play for %.3f seconds, using %u %stimestamped buffers of "
      "%u frames",
      duration_secs_, total_num_mapped_payloads_, (!use_pts_ ? "non-" : ""), frames_per_payload_);

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

// AudioCore interface is used to enable/disable the creation/update of device settings files,
// and to change the gain/volume of usages.
void MediaApp::SetAudioCoreSettings(sys::ComponentContext* app_context) {
  if (set_device_settings_ || set_usage_gain_ || set_usage_volume_) {
    fuchsia::media::AudioCorePtr audio_core;
    app_context->svc()->Connect(audio_core.NewRequest());

    if (set_device_settings_) {
      audio_core->EnableDeviceSettings(settings_enabled_);
    }

    if (set_usage_gain_) {
      audio_core->SetRenderUsageGain(usage_, usage_gain_db_);
    }

    if (set_usage_volume_) {
      fuchsia::media::Usage usage;
      usage.set_render_usage(usage_);
      audio_core->BindUsageVolumeControl(std::move(usage), usage_volume_control_.NewRequest());

      usage_volume_control_.set_error_handler([this](zx_status_t status) {
        FX_PLOGS(ERROR, status) << "Client connection to fuchsia.media.audio.VolumeControl failed";
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
  audio_renderer_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Client connection to fuchsia.media.AudioRenderer failed";
    Shutdown();
  });

  audio_renderer_->BindGainControl(gain_control_.NewRequest());
  gain_control_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Client connection to fuchsia.media.audio.GainControl failed";
    Shutdown();
  });

  // ... now just let the instance of audio go out of scope.
}

// Set the AudioRenderer's audio format to stereo 48kHz 16-bit (LPCM).
void MediaApp::SetStreamType() {
  FX_DCHECK(audio_renderer_);

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
      FX_PLOGS(ERROR, status) << "VmoMapper:::CreateAndMap failed";
      return status;
    }

    audio_renderer_->AddPayloadBuffer(i, std::move(payload_vmo));
  }

  return ZX_OK;
}

// We have a set of buffers each backed by its own VMO, with each buffer sub-divided into
// uniformly-sized zones, called payloads.
//
// We round robin packets across each buffer, wrapping around to the start of each buffer once the
// end is encountered. For example, with 2 buffers that can each hold 2 payloads each, we would
// send audio packets in the following order:
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
MediaApp::AudioPacket MediaApp::CreateAudioPacket(uint64_t payload_num) {
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = payload_num % num_payload_buffers_;

  auto buffer_payload_index = payload_num / num_payload_buffers_;
  packet.payload_offset = (buffer_payload_index % payloads_per_mapping_) * payload_size_;

  // If last payload, send exactly what remains (otherwise send a full payload).
  packet.payload_size =
      (payload_num + 1 == num_packets_to_send_)
          ? (total_frames_to_send_ - (payload_num * frames_per_payload_)) * frame_size_
          : payload_size_;

  // packet.pts is NO_TIMESTAMP by default unless we override it.
  if (use_pts_) {
    packet.pts = payload_num * frames_per_payload_;
  }

  return {
      .stream_packet = std::move(packet),
      .vmo = &payload_buffers_[packet.payload_buffer_id],
  };
}

void MediaApp::GenerateAudioForPacket(const AudioPacket& audio_packet, uint64_t payload_num) {
  const auto& packet = audio_packet.stream_packet;
  auto audio_buff = reinterpret_cast<uint8_t*>(audio_packet.vmo->start()) + packet.payload_offset;

  // Recompute payload_frames each time, since the final packet may be 'short'.
  //
  // TODO(mpuryear): don't recompute this every time; use payload_frames_ (and pre-compute this)
  // except for last packet, which we either check for here or pass in as a boolean parameter.
  uint32_t payload_frames = packet.payload_size / frame_size_;

  if (use_int24_) {
    WriteAudioIntoBuffer<int32_t>(reinterpret_cast<int32_t*>(audio_buff), payload_frames,
                                  frames_per_payload_ * payload_num, output_signal_type_,
                                  num_channels_, frames_per_period_, amplitude_scalar_);
  } else if (use_int16_) {
    WriteAudioIntoBuffer<int16_t>(reinterpret_cast<int16_t*>(audio_buff), payload_frames,
                                  frames_per_payload_ * payload_num, output_signal_type_,
                                  num_channels_, frames_per_period_, amplitude_scalar_);
  } else {
    WriteAudioIntoBuffer<float>(reinterpret_cast<float*>(audio_buff), payload_frames,
                                frames_per_payload_ * payload_num, output_signal_type_,
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
void MediaApp::SendPacket(uint64_t payload_num) {
  auto packet = CreateAudioPacket(payload_num);

  GenerateAudioForPacket(packet, payload_num);

  if (save_to_file_) {
    if (!wav_writer_.Write(
            reinterpret_cast<char*>(packet.vmo->start()) + packet.stream_packet.payload_offset,
            packet.stream_packet.payload_size)) {
      FX_LOGS(ERROR) << "WavWriter::Write() failed";
    }
  }

  ++num_packets_sent_;
  audio_renderer_->SendPacket(packet.stream_packet, [this]() { OnSendPacketComplete(); });
}

void MediaApp::OnSendPacketComplete() {
  ++num_packets_completed_;
  FX_DCHECK(num_packets_completed_ <= num_packets_to_send_);

  if (num_packets_sent_ < num_packets_to_send_) {
    SendPacket(num_packets_sent_);
  } else if (num_packets_completed_ >= num_packets_to_send_) {
    Shutdown();
  }
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon ~MediaApp).
void MediaApp::Shutdown() {
  if (wav_writer_is_initialized_) {
    if (!wav_writer_.Close()) {
      FX_LOGS(ERROR) << "WavWriter::Close() failed";
    }
  }

  payload_buffers_.clear();
  quit_callback_();
}

}  // namespace media::tools
