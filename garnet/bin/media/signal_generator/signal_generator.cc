// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/signal_generator/signal_generator.h"

#include <fbl/algorithm.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <zircon/syscalls.h>

#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace media::tools {

MediaApp::MediaApp(fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)) {
  FXL_DCHECK(quit_callback_);
}

// Prepare for playback, submit initial data, start the presentation timeline.
void MediaApp::Run(component::StartupContext* app_context) {
  if (!ParameterRangeChecks()) {
    Shutdown();
    return;
  }

  SetupPayloadCoefficients();
  DisplayConfigurationSettings();
  AcquireAudioRenderer(app_context);
  SetStreamType();

  if (CreateMemoryMapping() != ZX_OK) {
    Shutdown();
    return;
  }

  // 24-bit buffers use 32-bit samples (lowest byte zero), and when this
  // particular utility saves to .wav file, we save the entire 32 bits.
  if (save_to_file_) {
    if (!wav_writer_.Initialize(
            file_name_.c_str(),
            use_int24_
                ? fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32
                : (use_int16_ ? fuchsia::media::AudioSampleFormat::SIGNED_16
                              : fuchsia::media::AudioSampleFormat::FLOAT),
            num_channels_, frame_rate_, sample_size_ * 8)) {
      FXL_LOG(ERROR) << "WavWriter::Initialize() failed";
    } else {
      wav_writer_is_initialized_ = true;
    }
  }

  if (num_packets_to_send_ > 0) {
    uint32_t num_payloads_to_prime =
        fbl::min<uint64_t>(payloads_per_total_mapping_, num_packets_to_send_);
    for (uint32_t payload_num = 0; payload_num < num_payloads_to_prime;
         ++payload_num) {
      SendPacket(payload_num);
    }

    audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                                 fuchsia::media::NO_TIMESTAMP);
  } else {
    Shutdown();
  }
}

bool MediaApp::ParameterRangeChecks() {
  bool ret_val = true;

  if (num_channels_ < fuchsia::media::MIN_PCM_CHANNEL_COUNT) {
    FXL_LOG(ERROR) << "Number of channels must be at least "
                   << fuchsia::media::MIN_PCM_CHANNEL_COUNT;
    ret_val = false;
  }
  if (num_channels_ > fuchsia::media::MAX_PCM_CHANNEL_COUNT) {
    FXL_LOG(ERROR) << "Number of channels must be no greater than "
                   << fuchsia::media::MAX_PCM_CHANNEL_COUNT;
    ret_val = false;
  }

  if (frame_rate_ < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) {
    FXL_LOG(ERROR) << "Frame rate must be at least "
                   << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND;
    ret_val = false;
  }
  if (frame_rate_ > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND) {
    FXL_LOG(ERROR) << "Frame rate must be no greater than "
                   << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND;
    ret_val = false;
  }

  if (frequency_ < 0.0) {
    FXL_LOG(ERROR) << "Frequency cannot be negative";
    ret_val = false;
  }

  if (amplitude_ > 1.0) {
    FXL_LOG(ERROR) << "Amplitude must be no greater than 1.0";
    ret_val = false;
  }
  if (amplitude_ < -1.0) {
    FXL_LOG(ERROR) << "Amplitude must be no less than -1.0";
    ret_val = false;
  }

  if (duration_secs_ < 0.0) {
    FXL_LOG(ERROR) << "Duration cannot be negative";
    ret_val = false;
  }

  if (frames_per_payload_ > frame_rate_ / 2) {
    FXL_LOG(ERROR) << "Payload size must be 500 milliseconds or less.";
    ret_val = false;
  }
  if (frames_per_payload_ < frame_rate_ / 1000) {
    FXL_LOG(ERROR) << "Payload size must be 1 millisecond or more.";
    ret_val = false;
  }

  stream_gain_db_ =
      fbl::clamp<float>(stream_gain_db_, fuchsia::media::audio::MUTED_GAIN_DB,
                        fuchsia::media::audio::MAX_GAIN_DB);

  system_gain_db_ = fbl::clamp<float>(
      system_gain_db_, fuchsia::media::audio::MUTED_GAIN_DB, 0.0f);

  return ret_val;
}

// Based on the user-specified values for signal frequency and milliseconds per
// payload, calculate the other related coefficients needed for our mapped
// memory section, and for our series of payloads that reference that section.
//
// We share a memory section with our AudioRenderer, divided into equally-sized
// payloads (size specified by the user). For now, we trim the end of the memory
// section, rather than handle the occasional irregularly-sized packet.
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
  sample_size_ = use_int24_ ? sizeof(int32_t)
                            : (use_int16_ ? sizeof(int16_t) : sizeof(float));
  frame_size_ = num_channels_ * sample_size_;

  payload_size_ = frames_per_payload_ * frame_size_;

  // First, assume one second of audio, then determine how many payloads will
  // fit, then trim the mapping down to an amount that will actually be used.
  total_mapping_size_ = frame_rate_ * frame_size_;
  payloads_per_total_mapping_ = total_mapping_size_ / payload_size_;
  total_mapping_size_ = payloads_per_total_mapping_ * payload_size_;
}

void MediaApp::DisplayConfigurationSettings() {
  printf("\nAudioRenderer configured for %d-channel %s at %u Hz.\nContent is ",
         num_channels_,
         (use_int24_ ? "int24" : (use_int16_ ? "int16" : "float32")),
         frame_rate_);

  if (output_signal_type_ == kOutputTypeNoise) {
    printf("white noise");
  } else {
    printf("a %f Hz %s wave", frequency_,
           (output_signal_type_ == kOutputTypeSquare)
               ? "square"
               : (output_signal_type_ == kOutputTypeSawtooth) ? "triangle"
                                                              : "sine");
  }

  printf(", amplitude %f", amplitude_);
  if (ramp_stream_gain_) {
    printf(
        ",\nramping stream gain from %.3f dB to %.3f dB over %.6lf seconds "
        "(%ld nanoseconds)",
        stream_gain_db_, ramp_target_gain_db_,
        static_cast<double>(ramp_duration_nsec_) / 1000000000,
        ramp_duration_nsec_);
  } else if (set_stream_gain_) {
    printf(", at stream gain %.3f dB", stream_gain_db_);
  }
  if (set_stream_mute_) {
    printf(", after explicitly %smuting this stream", stream_mute_ ? "" : "un");
  }

  printf(".\nSignal will play for %.3f seconds, using %u buffers of %u frames",
         duration_secs_, payloads_per_total_mapping_, frames_per_payload_);

  if (set_system_gain_ || set_system_mute_) {
    printf(", after setting ");
  }
  if (set_system_gain_) {
    printf("System Gain to %.3f dB%s", system_gain_db_,
           set_system_mute_ ? " and " : "");
  }
  if (set_system_mute_) {
    printf("System Mute to %s", system_mute_ ? "TRUE" : "FALSE");
  }
  printf(".\n\n");
}

// Use StartupContext to acquire AudioPtr; use that to acquire AudioRendererPtr
// in turn. Set AudioRenderer error handler, in case of channel closure.
void MediaApp::AcquireAudioRenderer(component::StartupContext* app_context) {
  // The Audio interface is only needed to create AudioRenderer, set routing
  // policy and set system gain/mute. Use the synchronous proxy, for simplicity.
  fuchsia::media::AudioSyncPtr audio;
  app_context->ConnectToEnvironmentService(audio.NewRequest());

  if (set_system_gain_) {
    audio->SetSystemGain(system_gain_db_);
  }

  if (set_system_mute_) {
    audio->SetSystemMute(system_mute_);
  }

  if (set_policy_) {
    audio->SetRoutingPolicy(audio_policy_);
  }

  audio->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_->BindGainControl(gain_control_.NewRequest());

  audio_renderer_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR)
        << "Client connection to fuchsia.media.AudioRenderer failed: "
        << status;
    Shutdown();
  });

  gain_control_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Client connection to fuchsia.media.GainControl failed: "
                   << status;
    Shutdown();
  });
}

// Set the AudioRenderer's audio format to stereo 48kHz 16-bit (LPCM).
void MediaApp::SetStreamType() {
  FXL_DCHECK(audio_renderer_);

  fuchsia::media::AudioStreamType format;

  format.sample_format =
      (use_int24_ ? fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32
                  : (use_int16_ ? fuchsia::media::AudioSampleFormat::SIGNED_16
                                : fuchsia::media::AudioSampleFormat::FLOAT));
  format.channels = num_channels_;
  format.frames_per_second = frame_rate_;

  audio_renderer_->SetPcmStreamType(format);

  // Set stream gain and mute, if specified.
  if (set_stream_mute_) {
    gain_control_->SetMute(stream_mute_);
  }
  if (set_stream_gain_) {
    gain_control_->SetGain(stream_gain_db_);
  }
  if (ramp_stream_gain_) {
    gain_control_->SetGainWithRamp(
        ramp_target_gain_db_, ramp_duration_nsec_,
        fuchsia::media::audio::RampType::SCALE_LINEAR);
  }
}

// Create one Virtual Memory Object and map enough memory for 1 second of audio.
// Reduce rights and send handle to AudioRenderer: this is our shared buffer.
zx_status_t MediaApp::CreateMemoryMapping() {
  zx::vmo payload_vmo;
  zx_status_t status = payload_buffer_.CreateAndMap(
      total_mapping_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
      &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VmoMapper:::CreateAndMap failed - " << status;
    return status;
  }

  audio_renderer_->AddPayloadBuffer(0, std::move(payload_vmo));

  return ZX_OK;
}

// We divided our cross-proc buffer into different zones, called payloads.
// Create a packet corresponding to this particular payload.
fuchsia::media::StreamPacket MediaApp::CreateAudioPacket(uint64_t payload_num) {
  fuchsia::media::StreamPacket packet;

  packet.payload_offset =
      (payload_num % payloads_per_total_mapping_) * payload_size_;

  // If last payload, send exactly what remains (otherwise send a full payload).
  packet.payload_size =
      (payload_num + 1 == num_packets_to_send_)
          ? (total_frames_to_send_ - (payload_num * frames_per_payload_)) *
                frame_size_
          : payload_size_;

  return packet;
}

void MediaApp::GenerateAudioForPacket(fuchsia::media::StreamPacket packet,
                                      uint64_t payload_num) {
  auto audio_buff = reinterpret_cast<uint8_t*>(payload_buffer_.start()) +
                    packet.payload_offset;

  // Recompute payload_frames each time, since the final packet may be
  // 'short'.
  //
  // TODO(mpuryear): don't recompute this every time; use payload_frames_ (and
  // pre-compute this) except for last packet, which we either check for here
  // or pass in as a boolean parameter.
  uint32_t payload_frames = packet.payload_size / frame_size_;

  if (use_int24_) {
    WriteAudioIntoBuffer<int32_t>(
        reinterpret_cast<int32_t*>(audio_buff), payload_frames,
        frames_per_payload_ * payload_num, output_signal_type_, num_channels_,
        frames_per_period_, amplitude_scalar_);
  } else if (use_int16_) {
    WriteAudioIntoBuffer<int16_t>(
        reinterpret_cast<int16_t*>(audio_buff), payload_frames,
        frames_per_payload_ * payload_num, output_signal_type_, num_channels_,
        frames_per_period_, amplitude_scalar_);
  } else {
    WriteAudioIntoBuffer<float>(
        reinterpret_cast<float*>(audio_buff), payload_frames,
        frames_per_payload_ * payload_num, output_signal_type_, num_channels_,
        frames_per_period_, amplitude_scalar_);
  }
}

// Write signal into the next section of our buffer. Track how many total
// frames since playback started, to handle arbitrary frequencies of type
// double.
template <typename SampleType>
void MediaApp::WriteAudioIntoBuffer(
    SampleType* audio_buffer, uint32_t num_frames, uint64_t frames_since_start,
    OutputSignalType signal_type, uint32_t num_chans, double frames_per_period,
    double amp_scalar) {
  double raw_val;  // Generated signal val, before applying amplitude scaling.
  double rads_per_frame = 2.0 * M_PI / frames_per_period;  // Radians/Frame.

  for (uint32_t frame = 0; frame < num_frames; ++frame, ++frames_since_start) {
    switch (signal_type) {
      case kOutputTypeSine:
        raw_val = sin(rads_per_frame * frames_since_start);
        break;
      case kOutputTypeSquare:
        raw_val = (fmod(frames_since_start, frames_per_period) >=
                   frames_per_period / 2)
                      ? -1.0
                      : 1.0;
        break;
      case kOutputTypeSawtooth:
        raw_val =
            (fmod(frames_since_start / frames_per_period, 1.0) * 2.0) - 1.0;
        break;
      case kOutputTypeNoise:
        // TODO(mpuryear): consider making the white noise generator even more
        // truly random, with multiple rand() calls at different frequencies.
        raw_val = static_cast<double>(rand()) / RAND_MAX * 2.0 - 1.0;
        break;
    }

    SampleType val = raw_val * amp_scalar;

    // If generating a 24-in-32 signal, clear the unused bottom 8 bits.
    if (std::is_same<SampleType, int32_t>::value) {
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
  fuchsia::media::StreamPacket packet = CreateAudioPacket(payload_num);

  GenerateAudioForPacket(packet, payload_num);

  if (save_to_file_) {
    if (!wav_writer_.Write(reinterpret_cast<char*>(payload_buffer_.start()) +
                               packet.payload_offset,
                           packet.payload_size)) {
      FXL_LOG(ERROR) << "WavWriter::Write() failed";
    }
  }

  ++num_packets_sent_;
  audio_renderer_->SendPacket(packet, [this]() { OnSendPacketComplete(); });
}

void MediaApp::OnSendPacketComplete() {
  ++num_packets_completed_;
  FXL_DCHECK(num_packets_completed_ <= num_packets_to_send_);

  if (num_packets_sent_ < num_packets_to_send_) {
    SendPacket(num_packets_sent_);
  } else if (num_packets_completed_ >= num_packets_to_send_) {
    Shutdown();
  }
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon
// ~MediaApp).
void MediaApp::Shutdown() {
  if (wav_writer_is_initialized_) {
    if (!wav_writer_.Close()) {
      FXL_LOG(ERROR) << "WavWriter::Close() failed";
    }
  }

  payload_buffer_.Unmap();
  quit_callback_();
}

}  // namespace media::tools
