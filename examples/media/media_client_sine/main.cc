// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <math.h>
#include <zircon/syscalls.h>

#include "garnet/public/lib/media/c/audio.h"

// Set the renderer format to: 48 kHz, stereo, 16-bit LPCM (signed integer).
constexpr float kRendererFrameRate = 48000.0f;
constexpr size_t kNumChannels = 2;
// For this example, feed audio to the system in payloads of 10 milliseconds.
constexpr size_t kNumFramesPerBuffer = 480;
constexpr size_t kNumSamplesPerBuffer = kNumFramesPerBuffer * kNumChannels;
// Play a sine wave that is 439 Hz, at approximately 1/8 of full-scale volume.
constexpr float kFrequency = 439.0f;
constexpr float kOutputGain = -18.0f;
constexpr float kFrequencyScalar = kFrequency * 2 * M_PI / kRendererFrameRate;
// Loop for 2 seconds.
constexpr size_t kTotalDurationSecs = 2;
constexpr size_t kNumBuffersToSend =
    kTotalDurationSecs * kRendererFrameRate / kNumFramesPerBuffer;
constexpr zx_time_t kBufferNSecs =
    ZX_SEC(kTotalDurationSecs) / kNumBuffersToSend;

int main(int argc, const char** argv) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  if (manager == nullptr) {
    return -1;
  }

  int num_devices =
      fuchsia_audio_manager_get_output_devices(manager, nullptr, 0);
  if (num_devices == 0) {
    std::cout << "No output_devices - no problem, but nothing to do\n";
    fuchsia_audio_manager_free(manager);
    return 0;
  }

  // Applications may use the **fuchsia_audio_manager_get_output_devices** API
  // to enumerate devices, passing a fuchsia_audio_device_description array, as
  // well as the maximum number of devices to be retrieved.

  // Applications may retrieve a device's default (preferred) parameters using
  // the **fuchsia_audio_manager_get_output_device_default_parameters** API.

  // To make this example minimal, we open an output stream on the default
  // output device, using parameters that we know the audio system supports.
  fuchsia_audio_parameters params;
  params.sample_rate = kRendererFrameRate;
  params.num_channels = kNumChannels;
  params.buffer_size = kNumFramesPerBuffer;
  fuchsia_audio_output_stream* stream = nullptr;
  int status = fuchsia_audio_manager_create_output_stream(manager, nullptr,
                                                          &params, &stream);
  if (status < 0) {
    std::cout << "create_output_stream failed: " << status << "\n";
    fuchsia_audio_manager_free(manager);
    return -1;
  }

  zx_duration_t delay_ns;
  status = fuchsia_audio_output_stream_get_min_delay(stream, &delay_ns);
  if (status < 0) {
    std::cout << "stream_get_min_delay failed: " << status << "\n";
    fuchsia_audio_manager_free(manager);
    return -1;
  }

  status = fuchsia_audio_output_stream_set_gain(stream, kOutputGain);
  if (status < 0) {
    std::cout << "stream_set_gain failed: " << status << "\n";
    fuchsia_audio_manager_free(manager);
    return -1;
  }

  auto buffer =
      std::make_unique<float[]>(kNumSamplesPerBuffer * kNumBuffersToSend);
  for (size_t frame = 0; frame < kNumFramesPerBuffer * kNumBuffersToSend;
       ++frame) {
    float val = sin(kFrequencyScalar * frame);

    for (size_t chan_num = 0; chan_num < kNumChannels; ++chan_num) {
      buffer[frame * kNumChannels + chan_num] = val;
    }
  }

  zx_time_t first_write_time = zx_time_get(ZX_CLOCK_MONOTONIC);
  zx_time_t start_time = first_write_time + delay_ns + ZX_MSEC(1);

  zx_time_t timestamp = start_time;
  for (size_t write_num = 0; write_num < kNumBuffersToSend; ++write_num) {
    status = fuchsia_audio_output_stream_write(
        stream, buffer.get() + (kNumSamplesPerBuffer * write_num),
        kNumSamplesPerBuffer, timestamp);
    if (status < 0) {
      std::cout << "stream_write " << write_num << " failed: " << status
                << "\n";
      fuchsia_audio_manager_free(manager);
      return -1;
    }
    timestamp = FUCHSIA_AUDIO_NO_TIMESTAMP;

    // TODO(mpuryear): remove if stream_write can block on internal_buffer_full
    zx_time_t wake_time = first_write_time + write_num * kBufferNSecs;
    zx_nanosleep(wake_time);
  }

  // TODO(mpuryear): remove if stream_free can block until submitted audio ends
  zx_nanosleep(start_time + ZX_SEC(kTotalDurationSecs));

  // stream_free completes all already-submitted buffers before returning
  // TODO(mpuryear): ensure the client lib does this.
  fuchsia_audio_output_stream_free(stream);
  fuchsia_audio_manager_free(manager);
  return 0;
}
