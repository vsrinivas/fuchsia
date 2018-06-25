// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/signal_generator/signal_generator.h"

#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <zircon/syscalls.h>

// #include "lib/app/cpp/connect.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace media {
namespace tools {

MediaApp::MediaApp(fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)) {
  FXL_DCHECK(quit_callback_);
}

// Prepare for playback, submit initial data, start the presentation timeline.
void MediaApp::Run(fuchsia::sys::StartupContext* app_context) {
  if (!SetupPayloadCoefficients()) {
    Shutdown();
    return;
  }

  printf("\nRenderer configured for %d-channel %s at %u Hz.\nContent is ",
         num_channels_, (use_int16_) ? "int16" : "float32", frame_rate_);

  if (output_signal_type_ == kOutputTypeNoise) {
    printf("white noise");
  } else {
    printf("a %u Hz %s wave", frequency_,
           (output_signal_type_ == kOutputTypeSquare)
               ? "square"
               : (output_signal_type_ == kOutputTypeSawtooth) ? "triangle"
                                                              : "sine");
  }

  printf(" (amplitude %f, renderer gain %.2f dB).\n", amplitude_,
         renderer_gain_db_);

  printf("Signal will play for %u seconds, using %zu %u-msec buffers.\n\n",
         duration_secs_, num_payloads_, msecs_per_payload_);

  AcquireRenderer(app_context);
  SetMediaType();

  if (CreateMemoryMapping() != ZX_OK) {
    Shutdown();
    return;
  }

  if (use_int16_) {
    WriteAudioIntoBuffer<int16_t>(
        reinterpret_cast<int16_t*>(payload_buffer_.start()), frames_per_period_,
        amplitude_scalar_, frames_per_payload_ * num_payloads_, num_channels_,
        output_signal_type_);
  } else {
    WriteAudioIntoBuffer<float>(
        reinterpret_cast<float*>(payload_buffer_.start()), frames_per_period_,
        amplitude_scalar_, frames_per_payload_ * num_payloads_, num_channels_,
        output_signal_type_);
  }

  if (save_to_file_) {
    if (!wav_writer_.Initialize(
            file_name_.c_str(),
            use_int16_ ? fuchsia::media::AudioSampleFormat::SIGNED_16
                       : fuchsia::media::AudioSampleFormat::FLOAT,
            num_channels_, frame_rate_, sample_size_ * 8)) {
      FXL_LOG(ERROR) << "WavWriter::Initialize() failed";
    }
  }

  for (size_t payload_num = 0; payload_num < num_payloads_; ++payload_num) {
    SendPacket(CreateAudioPacket(payload_num));
  }

  audio_renderer_->PlayNoReply(fuchsia::media::kNoTimestamp,
                               fuchsia::media::kNoTimestamp);
}

// Based on the user-specified values for signal frequency and milliseconds per
// payload, calculate the other related coefficients needed for our mapped
// memory section, and for our series of payloads that reference that section.
//
// For now, this tool uses a mapped memory section that is exactly 1
// second, dividing that mapping into a number of payloads that fit perfectly
// into that mapping, and sending one of these equally-sized payloads with each
// packet. This imposes a few constraints that will be removed (soon) when we
// make the change to generate the signal on the fly.
bool MediaApp::SetupPayloadCoefficients() {
  if ((msecs_per_payload_ * frame_rate_) % 1000 != 0) {
    FXL_LOG(ERROR) << "frame_rate and msecs_per_payload are incompatible.";
    return false;
  }

  if (msecs_per_payload_ > 1000) {
    FXL_LOG(ERROR) << "msecs_per_payload cannot be larger than 1000.";
    return false;
  }
  // This msec_per_payload value means we will only have one buffer outstanding
  // at any time. Unless we are only playing the buffer once, we cannot reliably
  // stream without data loss.
  if (msecs_per_payload_ == 1000 && duration_secs_ > 1) {
    FXL_LOG(ERROR) << "reduce msecs_per_payload or duration.";
    return false;
  }

  // TODO(mpuryear): Change the msecs_per_payload parameter to instead be
  // frames_per_payload. This, in concert with the change to generate the signal
  // on-the-fly for each packet (instead of pre-generating and looping 1 second
  // of signal), enables users to specify any payload size, duration or signal
  // frequency (duration and frequency will in fact change from int to double).
  if (1000 % msecs_per_payload_ != 0) {
    FXL_LOG(ERROR) << "msecs_per_payload must divide evenly into 1000.";
    return false;
  }

  frames_per_payload_ = msecs_per_payload_ * frame_rate_ / 1000;
  num_packets_to_send_ = duration_secs_ * 1000 / msecs_per_payload_;

  // Number of frames in each period of the recurring signal.
  frames_per_period_ = frame_rate_ / frequency_;

  amplitude_scalar_ = amplitude_;
  if (use_int16_) {
    amplitude_scalar_ *= std::numeric_limits<int16_t>::max();
  }

  sample_size_ = (use_int16_) ? sizeof(int16_t) : sizeof(float);

  payload_size_ = frames_per_payload_ * num_channels_ * sample_size_;

  // As mentioned above, the mapped memory section is exactly 1 second of audio.
  num_payloads_ = 1000 / msecs_per_payload_;
  payload_mapping_size_ = payload_size_ * num_payloads_;

  return true;
}

// Use StartupContext to acquire AudioPtr and AudioRenderer2Ptr in turn. Set
// error handler, in case of channel closure.
void MediaApp::AcquireRenderer(fuchsia::sys::StartupContext* app_context) {
  // The Audio interface is needed only long enough to create the renderer(s).
  fuchsia::media::AudioSync2Ptr audio;
  fuchsia::sys::ConnectToEnvironmentService(audio.NewRequest());

  if (set_system_gain_) {
    audio->SetSystemGain(system_gain_db_);
    audio->SetSystemMute(false);
  }
  if (set_policy_) {
    audio->SetRoutingPolicy(audio_policy_);
  }

  audio->CreateRendererV2(audio_renderer_.NewRequest());

  audio_renderer_.set_error_handler([this]() {
    FXL_LOG(ERROR)
        << "fuchsia::media::AudioRenderer connection lost. Quitting.";
    Shutdown();
  });
}

// Set the Mediarenderer's audio format to stereo 48kHz 16-bit (LPCM).
void MediaApp::SetMediaType() {
  FXL_DCHECK(audio_renderer_);

  fuchsia::media::AudioPcmFormat format;

  format.sample_format =
      (use_int16_ ? fuchsia::media::AudioSampleFormat::SIGNED_16
                  : fuchsia::media::AudioSampleFormat::FLOAT);
  format.channels = num_channels_;
  format.frames_per_second = frame_rate_;

  audio_renderer_->SetPcmFormat(std::move(format));

  // Set renderer gain, and clear the mute status.
  audio_renderer_->SetGainMuteNoReply(
      renderer_gain_db_, false,
      fuchsia::media::kGainFlagGainValid | fuchsia::media::kGainFlagMuteValid);
}

// Create a single Virtual Memory Object, and map enough memory for our audio
// buffers. Reduce the rights and send the handle over to the AudioRenderer to
// act as our shared buffer.
zx_status_t MediaApp::CreateMemoryMapping() {
  zx::vmo payload_vmo;
  zx_status_t status = payload_buffer_.CreateAndMap(
      payload_mapping_size_, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
      nullptr, &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VmoMapper:::CreateAndMap failed - " << status;
    return status;
  }

  audio_renderer_->SetPayloadBuffer(std::move(payload_vmo));

  return ZX_OK;
}

// Write a sine wave into our audio buffer. We'll continuously loop/resubmit it.
template <typename SampleType>
void MediaApp::WriteAudioIntoBuffer(SampleType* audio_buffer,
                                    double frames_per_period, double amp_scalar,
                                    size_t num_frames, uint32_t num_chans,
                                    OutputSignalType signal_type) {
  double raw_val;  // Generated signal val, before applying amplitude scaling.
  double rads_per_frame = 2.0 * M_PI / frames_per_period;  // Radians/Frame.

  for (size_t frame = 0; frame < num_frames; ++frame) {
    switch (signal_type) {
      case kOutputTypeNoise:
        // TODO(mpuryear): consider making the white noise generator more truly
        // random (with multiple rand calls at different frequencies)
        raw_val = static_cast<double>(rand()) / RAND_MAX * 2.0 - 1.0;
        break;
      case kOutputTypeSawtooth:
        raw_val = (fmod(frame / frames_per_period, 1.0) * 2.0) - 1.0;
        break;
      case kOutputTypeSquare:
        raw_val = (fmod(frame, frames_per_period) >= frames_per_period / 2)
                      ? -1.0
                      : 1.0;
        break;
      default:
        raw_val = sin(rads_per_frame * frame);
    }

    for (size_t chan_num = 0; chan_num < num_chans; ++chan_num) {
      audio_buffer[frame * num_chans + chan_num] = raw_val * amp_scalar;
    }
  }
}

// We divided our cross-proc buffer into different zones, called payloads.
// Create a packet corresponding to this particular payload.
fuchsia::media::AudioPacket MediaApp::CreateAudioPacket(size_t payload_num) {
  fuchsia::media::AudioPacket packet;
  packet.payload_offset = (payload_num * payload_size_) % payload_mapping_size_;
  packet.payload_size = payload_size_;
  return packet;
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if there are more packets to send, create and send the next packet;
// b. if all expected packets have completed, begin closing down the system.
void MediaApp::SendPacket(fuchsia::media::AudioPacket packet) {
  if (save_to_file_) {
    if (!wav_writer_.Write(reinterpret_cast<char*>(payload_buffer_.start()) +
                               packet.payload_offset,
                           packet.payload_size)) {
      FXL_LOG(ERROR) << "WavWriter::Write() failed";
    }
  }

  ++num_packets_sent_;
  audio_renderer_->SendPacket(std::move(packet),
                              [this]() { OnSendPacketComplete(); });
}

void MediaApp::OnSendPacketComplete() {
  ++num_packets_completed_;
  FXL_DCHECK(num_packets_completed_ <= num_packets_to_send_);

  if (num_packets_sent_ < num_packets_to_send_) {
    SendPacket(CreateAudioPacket(num_packets_sent_));
  } else if (num_packets_completed_ >= num_packets_to_send_) {
    Shutdown();
  }
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon
// ~MediaApp).
void MediaApp::Shutdown() {
  if (save_to_file_) {
    if (!wav_writer_.Close()) {
      FXL_LOG(ERROR) << "WavWriter::Close() failed";
    }
  }

  payload_buffer_.Unmap();
  quit_callback_();
}

}  // namespace tools
}  // namespace media