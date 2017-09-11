// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/audio_input.h"

#include <audio-utils/audio-input.h>
#include <errno.h>
#include <fcntl.h>
#include <magenta/device/audio.h>
#include <fbl/auto_call.h>
#include <fbl/vector.h>

#include "lib/media/timeline/timeline_function.h"
#include "lib/media/timeline/timeline_rate.h"
#include "garnet/bin/media/audio/driver_utils.h"
#include "lib/fxl/logging.h"

namespace media {

constexpr uint32_t AudioInput::kPacketsPerRingBuffer;
constexpr uint32_t AudioInput::kPacketsPerSecond;

// static
std::shared_ptr<AudioInput> AudioInput::Create(const std::string& device_path) {
  std::shared_ptr<AudioInput> device(new AudioInput(device_path));

  mx_status_t result = device->Initalize();
  if (result != MX_OK) {
    FXL_LOG(ERROR) << "Failed to open and initialize audio input device \""
                   << device_path << "\" (res " << result << ")";
    return nullptr;
  }

  return device;
}

AudioInput::AudioInput(const std::string& device_path)
    : allocator_(PayloadAllocator::GetDefault()),
      state_(State::kUninitialized) {
  audio_input_ = audio::utils::AudioInput::Create(device_path.c_str());
}

mx_status_t AudioInput::Initalize() {
  if (state_ != State::kUninitialized) {
    return MX_ERR_BAD_STATE;
  }

  if (audio_input_ == nullptr) {
    return MX_ERR_NO_MEMORY;
  }

  mx_status_t res = audio_input_->Open();
  if (res != MX_OK) {
    return res;
  }

  fbl::Vector<audio_stream_format_range_t> formats;
  res = audio_input_->GetSupportedFormats(&formats);
  if (res != MX_OK) {
    return res;
  }

  for (const auto& fmt : formats) {
    driver_utils::AddAudioStreamTypeSets(fmt, &supported_stream_types_);
  }

  state_ = State::kStopped;

  return MX_OK;
}

AudioInput::~AudioInput() {
  Stop();
}

std::vector<std::unique_ptr<media::StreamTypeSet>>
AudioInput::GetSupportedStreamTypes() {
  std::vector<std::unique_ptr<media::StreamTypeSet>> result;

  for (const auto& t : supported_stream_types_) {
    result.push_back(t->Clone());
  }

  return result;
}

bool AudioInput::SetStreamType(std::unique_ptr<StreamType> stream_type) {
  FXL_DCHECK(stream_type);
  if (state_ != State::kStopped) {
    FXL_LOG(ERROR) << "SetStreamType called after Start";
    return false;
  }

  // We are in the proper state to accept a SetStreamType request.  If the
  // request fails for any reason, the internal configuration should be
  // considered to be invalid.
  config_valid_ = false;
  bool compatible = false;
  for (const auto& t : supported_stream_types_) {
    if (t->Includes(*stream_type)) {
      compatible = true;
      break;
    }
  }

  if (!compatible) {
    FXL_LOG(ERROR) << "Unsupported stream type requested";
    return false;
  }

  // Convert the AudioStreamType::SampleFormat into an audio_sample_format_t
  // which the driver will understand.  This should really never fail.
  const auto& audio_stream_type = *stream_type->audio();
  if (!driver_utils::SampleFormatToDriverSampleFormat(
          audio_stream_type.sample_format(), &configured_sample_format_)) {
    FXL_LOG(ERROR) << "Failed to convert SampleFormat ("
                   << static_cast<uint32_t>(audio_stream_type.sample_format())
                   << ") to audio_sample_format_t";
    return false;
  }

  configured_frames_per_second_ = audio_stream_type.frames_per_second();
  configured_channels_ = audio_stream_type.channels();
  configured_bytes_per_frame_ = audio_stream_type.bytes_per_frame();
  pts_rate_ = TimelineRate(configured_frames_per_second_, 1);
  config_valid_ = true;

  return true;
}

void AudioInput::Start() {
  FXL_DCHECK(state_ != State::kUninitialized);

  if (state_ != State::kStopped) {
    FXL_DCHECK(state_ == State::kStarted);
    return;
  }

  if (!config_valid_) {
    FXL_LOG(ERROR) << "Cannot start AudioInput.  "
                   << "Configuration is currently invalid.";
    return;
  }

  state_ = State::kStarted;
  worker_thread_ = std::thread([this]() { Worker(); });
}

void AudioInput::Stop() {
  FXL_DCHECK(state_ != State::kUninitialized);

  if (state_ != State::kStarted) {
    return;
  }

  state_ = State::kStopping;

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  FXL_DCHECK(state_ == State::kStopped);
}

bool AudioInput::can_accept_allocator() const {
  return true;
}

void AudioInput::set_allocator(PayloadAllocator* allocator) {
  allocator_ = allocator;
}

void AudioInput::SetDownstreamDemand(Demand demand) {}

void AudioInput::Worker() {
  FXL_DCHECK((state_ == State::kStarted) || (state_ == State::kStopping));
  FXL_DCHECK(config_valid_);

  mx_status_t res;
  uint32_t cached_frames_per_packet = frames_per_packet();
  uint32_t cached_packet_size = packet_size();

  auto cleanup = fbl::MakeAutoCall([this]() {
    audio_input_->ResetRingBuffer();
    state_ = State::kStopped;
  });

  // Configure the format.
  res =
      audio_input_->SetFormat(configured_frames_per_second_,
                              configured_channels_, configured_sample_format_);
  if (res != MX_OK) {
    FXL_LOG(ERROR) << "Failed set device format to "
                   << configured_frames_per_second_ << " Hz "
                   << configured_channels_ << " channel"
                   << (configured_channels_ == 1 ? "" : "s") << " fmt "
                   << configured_sample_format_ << " (res " << res << ")";
    return;
  }

  // Establish the shared ring buffer.  Request enough room to hold at least
  // kPacketPerRingBuffer packets.
  uint32_t rb_frame_count = cached_packet_size * kPacketsPerRingBuffer;
  res = audio_input_->GetBuffer(rb_frame_count, 0);
  if (res != MX_OK) {
    FXL_LOG(ERROR) << "Failed fetch ring buffer (" << rb_frame_count
                   << " frames, res = " << res << ")";
    return;
  }

  // Sanity check how much space we actually got.
  FXL_DCHECK(configured_bytes_per_frame_ != 0);
  if (audio_input_->ring_buffer_bytes() % configured_bytes_per_frame_) {
    FXL_LOG(ERROR) << "Error driver supplied ring buffer size ("
                   << audio_input_->ring_buffer_bytes()
                   << ") is not divisible by audio frame size ("
                   << configured_bytes_per_frame_ << ")";
    return;
  }
  rb_frame_count =
      audio_input_->ring_buffer_bytes() / configured_bytes_per_frame_;
  uint32_t rb_packet_count = rb_frame_count / cached_frames_per_packet;

  // Start capturing audio.
  res = audio_input_->StartRingBuffer();
  if (res != MX_OK) {
    FXL_LOG(ERROR) << "Failed to start capture (res = " << res << ")";
    return;
  }

  // Set up the transformation we will use to map from time to the safe write
  // pointer position in the ring buffer.
  int64_t frames_rxed = 0;
  int64_t fifo_frames =
      (audio_input_->fifo_depth() + configured_bytes_per_frame_ - 1) /
      configured_bytes_per_frame_;

  TimelineFunction ticks_to_wr_ptr(audio_input_->start_ticks(), -fifo_frames,
                                   mx_ticks_per_second(),
                                   configured_frames_per_second_);

  // TODO(johngro) : If/when the magenta APIs support specifying deadlines using
  // the tick timeline instead of the clock monotonic timeline, use that
  // instead.
  TimelineRate nsec_per_frame(1000000000u, configured_frames_per_second_);

  while (state_ == State::kStarted) {
    // Steady state operation.  Start by figuring out how many full packets we
    // have waiting for us in the ring buffer.
    uint64_t now_ticks = mx_ticks_get();
    int64_t wr_ptr = ticks_to_wr_ptr.Apply(now_ticks);
    int64_t pending_packets = (wr_ptr - frames_rxed) / cached_frames_per_packet;

    if (pending_packets > 0) {
      // If the number of pending packets is >= the number of packets which can
      // fit into the ring buffer, then we have clearly overflowed.  Print a
      // warning and skip the lost data.
      //
      // TODO(johngro) : We could produce payloads full of silence instead of
      // just skipping the data if we wanted to.  It seems wasteful, however,
      // since clients should be able to infer that data was lost based on the
      // timestamps placed on the packets.
      if (pending_packets >= rb_packet_count) {
        uint32_t skip_count = pending_packets - rb_packet_count + 1;

        FXL_DCHECK(pending_packets > skip_count);
        FXL_LOG(WARNING) << "Input overflowed by " << skip_count << " packets.";
        frames_rxed +=
            static_cast<uint64_t>(skip_count) * cached_frames_per_packet;
        pending_packets -= skip_count;
      }

      // Now produce as many packets as we can given our pending packet count.
      uint32_t modulo_rd_ptr_bytes =
          (frames_rxed % rb_frame_count) * configured_bytes_per_frame_;
      FXL_DCHECK(modulo_rd_ptr_bytes < audio_input_->ring_buffer_bytes());

      while (pending_packets > 0) {
        auto buf = reinterpret_cast<uint8_t*>(
            allocator_->AllocatePayloadBuffer(cached_packet_size));
        if (buf == nullptr) {
          FXL_LOG(ERROR) << "Allocator starved";
          return;
        }

        // Copy the data from the ring buffer into the packet we are producing.
        auto src =
            reinterpret_cast<const uint8_t*>(audio_input_->ring_buffer()) +
            modulo_rd_ptr_bytes;
        uint32_t contig_space =
            audio_input_->ring_buffer_bytes() - modulo_rd_ptr_bytes;
        if (contig_space >= cached_packet_size) {
          ::memcpy(buf, src, cached_packet_size);
          if (contig_space == cached_packet_size) {
            modulo_rd_ptr_bytes = 0;
          } else {
            modulo_rd_ptr_bytes += cached_packet_size;
          }
        } else {
          uint32_t leftover = cached_packet_size - contig_space;
          ::memcpy(buf, src, contig_space);
          ::memcpy(buf + contig_space, audio_input_->ring_buffer(), leftover);
          modulo_rd_ptr_bytes = leftover;
        }
        FXL_DCHECK(modulo_rd_ptr_bytes < audio_input_->ring_buffer_bytes());

        stage().SupplyPacket(Packet::Create(frames_rxed, pts_rate_, false,
                                            false, cached_packet_size, buf,
                                            allocator_));

        // Update our bookkeeping.
        pending_packets--;
        frames_rxed += cached_frames_per_packet;

        // Check to make sure we are not supposed to be stopping at this point.
        if (state_ != State::kStarted) {
          return;
        }
      }

      // TODO(johngro) : If it takes any significant amount of time to produce
      // and push the pending packets, we should re-compute the new position of
      // the write pointer based on the current tick time.
    }

    // Now figure out how long we will need to wait until we have at least one
    // new packet waiting for us in the ring.
    int64_t needed_frames = frames_rxed + cached_frames_per_packet + 1 - wr_ptr;
    int64_t sleep_nsec = nsec_per_frame.Scale(needed_frames);
    if (sleep_nsec > 0) {
      mx_nanosleep(mx_deadline_after(sleep_nsec));
    }
  }
}

}  // namespace media
