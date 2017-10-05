// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <string>
#include <vector>

#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

#include "garnet/public/lib/media/c/audio.h"

// Currently this file is a stub implementation of the interface in audio.h.
// TODO(mpuryear): Convert the stub implementation. Enumerate audio playback
// devices, connect to the selected devices, and play audio to said devices.
namespace {
constexpr int kStubNumDevices = 2;

static const char* kStubDeviceIds[kStubNumDevices] = {"dummy1", "dummy2"};
static const char* kStubDeviceNames[kStubNumDevices] = {"Dummy Audio Output 1",
                                                        "Dummy Audio Output 2"};
constexpr int kStubDeviceRates[kStubNumDevices] = {44100, 48000};
constexpr int kStubDeviceNumChans[kStubNumDevices] = {2, 1};
constexpr int kStubDeviceBufferSizes[kStubNumDevices] = {1024, 3000};
constexpr zx_duration_t kStubDeviceMinDelaysNSec[kStubNumDevices] = {20000000,
                                                                     100000000};
}  // namespace
// End of stub-related placeholder values

struct _fuchsia_audio_output_stream {
  fuchsia_audio_parameters stream_params;
  zx_time_t delay_nsec;
  bool received_first_pres_time;
  fuchsia_audio_manager* manager;
};

// At creation time, audio_manager constructs a vector to hold its associated
// streams. During audio_manager shutdown, it frees any leftover streams.
struct _fuchsia_audio_manager {
  std::vector<fuchsia_audio_output_stream*>* streams;
};

namespace {
// Soft limits only - can be expanded if needed (except kMinNumChannels).
constexpr int kMinSampleRate = 8000;
constexpr int kMaxSampleRate = 96000;
constexpr int kMinNumChannels = 1;
constexpr int kMaxNumChannels = 2;
}  // namespace

// Public API functions
fuchsia_audio_manager* fuchsia_audio_manager_create() {
  fuchsia_audio_manager* manager = new fuchsia_audio_manager();

  manager->streams = new std::vector<fuchsia_audio_output_stream*>();

  return manager;
}

void fuchsia_audio_manager_free(fuchsia_audio_manager* manager) {
  FXL_DCHECK(manager);

  while (!manager->streams->empty()) {
    fuchsia_audio_output_stream_free(manager->streams->back());
  }
  delete manager->streams;
  delete manager;
}

int fuchsia_audio_manager_get_output_devices(
    fuchsia_audio_manager* manager,
    fuchsia_audio_device_description* buffer,
    int num_device_descriptions) {
  FXL_DCHECK(manager);
  FXL_DCHECK((buffer == nullptr) == (num_device_descriptions == 0));
  FXL_DCHECK(num_device_descriptions >= 0);

  // TODO(mpuryear): if a callback (FIDL or other) returned an error since the
  // previous API call from this client, then return ZX_ERR_CONNECTION_ABORTED

  if (!buffer) {
    return kStubNumDevices;
  }

  int dev_num, num_to_copy = std::min(num_device_descriptions, kStubNumDevices);

  for (dev_num = 0; dev_num < num_to_copy; ++dev_num) {
    strncpy(buffer[dev_num].name, kStubDeviceNames[dev_num],
            sizeof(buffer[dev_num].name));
    strncpy(buffer[dev_num].id, kStubDeviceIds[dev_num],
            sizeof(buffer[dev_num].id));
  }
  return dev_num;
}

// Returns -1 if there is no device with the given ID.
static int get_device_num_by_id(char* device_id) {
  // If |device_id| is not specified or empty then use the default device (0).
  if (!device_id || !*device_id)
    return 0;
  for (int dev_num = 0; dev_num < kStubNumDevices; ++dev_num) {
    if (strcmp(kStubDeviceIds[dev_num], device_id) == 0) {
      return dev_num;
    }
  }
  return -1;
}

int fuchsia_audio_manager_get_output_device_default_parameters(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params) {
  FXL_DCHECK(manager);
  FXL_DCHECK(stream_params);

  // TODO(mpuryear): if a callback (FIDL or other) returned an error since the
  // previous API call from this client, then return ZX_ERR_CONNECTION_ABORTED

  int dev_num = get_device_num_by_id(device_id);
  if (dev_num < 0) {
    return ZX_ERR_NOT_FOUND;  // Device was removed after a previous call to
                              // fuchsia_audio_manager_get_output_devices
  }

  stream_params->sample_rate = kStubDeviceRates[dev_num];
  stream_params->num_channels = kStubDeviceNumChans[dev_num];
  stream_params->buffer_size = kStubDeviceBufferSizes[dev_num];
  return ZX_OK;
}

int fuchsia_audio_manager_create_output_stream(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params,
    fuchsia_audio_output_stream** stream_out) {
  FXL_DCHECK(manager);
  FXL_DCHECK(stream_params);
  FXL_DCHECK(stream_out);
  FXL_DCHECK(stream_params->num_channels >= kMinNumChannels);
  FXL_DCHECK(stream_params->num_channels <= kMaxNumChannels);
  FXL_DCHECK(stream_params->sample_rate >= kMinSampleRate);
  FXL_DCHECK(stream_params->sample_rate <= kMaxSampleRate);

  // TODO(mpuryear): if a callback (FIDL or other) returned an error since the
  // previous API call from this client, then return ZX_ERR_CONNECTION_ABORTED

  int dev_num =  get_device_num_by_id(device_id);
  if (dev_num < 0) {
    return ZX_ERR_NOT_FOUND;  // Device was removed after a previous call to
                              // fuchsia_audio_manager_get_output_devices
  }

  *stream_out = new fuchsia_audio_output_stream{
      .stream_params = *stream_params,
      .delay_nsec = kStubDeviceMinDelaysNSec[dev_num],
      .received_first_pres_time = false,
      .manager = manager,
  };

  manager->streams->push_back(*stream_out);

  return ZX_OK;
}

int fuchsia_audio_output_stream_free(fuchsia_audio_output_stream* stream) {
  FXL_DCHECK(stream);

  // TODO(mpuryear): if a callback (FIDL or other) returned an error since the
  // previous API call from this client, then return ZX_ERR_CONNECTION_ABORTED

  // TODO(mpuryear): What to do with already-submitted audio for this stream?
  // Letting it drain is not feasible: per contract, this API is synchronous.
  // Note: fuchsia_audio_manager_free() calls this, for any remaining streams.

  std::vector<fuchsia_audio_output_stream*>* streams = stream->manager->streams;

  for (auto str_iter = streams->begin(); str_iter != streams->end();
       ++str_iter) {
    if (*str_iter == stream) {
      streams->erase(str_iter);
      break;
    }
  }

  delete stream;
  return ZX_OK;
}

int fuchsia_audio_output_stream_get_min_delay(
    fuchsia_audio_output_stream* stream,
    zx_duration_t* delay_nsec_out) {
  FXL_DCHECK(stream);
  FXL_DCHECK(delay_nsec_out);

  // TODO(mpuryear): if a callback (FIDL or other) returned an error since the
  // previous API call from this client, then return ZX_ERR_CONNECTION_ABORTED

  *delay_nsec_out = stream->delay_nsec;
  return ZX_OK;
}

int fuchsia_audio_output_stream_write(fuchsia_audio_output_stream* stream,
                                      float* sample_buffer,
                                      int num_samples,
                                      zx_time_t pres_time) {
  FXL_DCHECK(stream);
  FXL_DCHECK(sample_buffer);
  FXL_DCHECK(num_samples > 0);
  FXL_DCHECK(num_samples % stream->stream_params.num_channels == 0);
  FXL_DCHECK(pres_time <= FUCHSIA_AUDIO_NO_TIMESTAMP);

  // TODO(mpuryear): if a callback (FIDL or other) returned an error since the
  // previous API call from this client, then return ZX_ERR_CONNECTION_ABORTED

  if (pres_time == FUCHSIA_AUDIO_NO_TIMESTAMP &&
      !stream->received_first_pres_time) {
    return ZX_ERR_BAD_STATE;
  }
  zx_time_t deadline = stream->delay_nsec + zx_time_get(ZX_CLOCK_MONOTONIC);
  if (pres_time < deadline) {
    return ZX_ERR_IO_MISSED_DEADLINE;
  }

  stream->received_first_pres_time = true;
  return ZX_OK;
}
