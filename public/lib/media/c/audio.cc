// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <string>
#include <vector>

#include <magenta/errors.h>
#include <magenta/syscalls.h>

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
constexpr mx_duration_t kStubDeviceMinDelaysNSec[kStubNumDevices] = {200000,
                                                                     100000000};
}  // namespace
// End of stub-related placeholder values

namespace {
// Guard against already-freed (or just plain bad) structs: store a 4-char tag
// in the first few bytes, and check for this before using the struct. This goes
// for both audio_manager and audio_output_stream structs.
constexpr size_t kTagSize = 4;
constexpr char kFuchsiaAudioManagerTag[] = "FAM ";
constexpr char kFuchsiaAudioOutputStreamTag[] = "FAOS";
constexpr char kFuchsiaAudioBlankTag[] = "    ";
}  // namespace

struct _fuchsia_audio_output_stream {
  char tag[kTagSize];
  fuchsia_audio_parameters stream_params;
  mx_time_t delay_nsec;
  bool received_first_pres_time;
  fuchsia_audio_manager* manager;
};

// At creation time, audio_manager constructs a vector to hold its associated
// streams. During audio_manager shutdown, it frees any leftover streams.
struct _fuchsia_audio_manager {
  char tag[kTagSize];
  std::vector<fuchsia_audio_output_stream*>* streams;
};

namespace {
// Soft limits only - can be expanded if needed (except kMinNumChannels).
constexpr int kMinSampleRate = 8000;
constexpr int kMaxSampleRate = 96000;
constexpr int kMinNumChannels = 1;
constexpr int kMaxNumChannels = 2;

// (Private) utility functions
bool is_valid_manager(fuchsia_audio_manager* manager) {
  return !strncmp(manager->tag, kFuchsiaAudioManagerTag, kTagSize);
}

bool is_valid_stream(fuchsia_audio_output_stream* stream) {
  return !strncmp(stream->tag, kFuchsiaAudioOutputStreamTag, kTagSize);
}
}  // namespace

// Public API functions
fuchsia_audio_manager* fuchsia_audio_manager_create() {
  fuchsia_audio_manager* manager = new fuchsia_audio_manager();

  manager->streams = new std::vector<fuchsia_audio_output_stream*>();
  strncpy(manager->tag, kFuchsiaAudioManagerTag, 4);

  return manager;
}

int fuchsia_audio_manager_free(fuchsia_audio_manager* manager) {
  FXL_DCHECK(manager);
  if (!is_valid_manager(manager)) {
    return MX_ERR_BAD_HANDLE;
  }

  strncpy(manager->tag, kFuchsiaAudioBlankTag, 4);
  while (!manager->streams->empty()) {
    fuchsia_audio_output_stream_free(manager->streams->back());
  }

  delete manager->streams;
  delete manager;
  return MX_OK;
}

int fuchsia_audio_manager_get_output_devices(
    fuchsia_audio_manager* manager,
    fuchsia_audio_device_description* buffer,
    int num_device_descriptions) {
  FXL_DCHECK(manager);
  if (!is_valid_manager(manager)) {
    return MX_ERR_BAD_HANDLE;
  }

  if (!buffer != !num_device_descriptions) {
    return MX_ERR_INVALID_ARGS;
  }
  if (num_device_descriptions < 0) {
    return MX_ERR_OUT_OF_RANGE;
  }

  if (!buffer) {
    return kStubNumDevices;
  }

  int num_to_copy = std::min(num_device_descriptions, kStubNumDevices);

  for (int dev_num = 0; dev_num < num_to_copy; ++dev_num) {
    strncpy(buffer[dev_num].name, kStubDeviceNames[dev_num],
            sizeof(buffer[dev_num].name));
    strncpy(buffer[dev_num].id, kStubDeviceIds[dev_num],
            sizeof(buffer[dev_num].id));
  }
  return num_to_copy;
}

int fuchsia_audio_manager_get_output_device_default_parameters(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params) {
  FXL_DCHECK(manager);
  if (!is_valid_manager(manager)) {
    return MX_ERR_BAD_HANDLE;
  }

  if (!device_id || !strlen(device_id)) {
    return MX_ERR_INVALID_ARGS;
  }

  if (!stream_params) {
    return MX_ERR_INVALID_ARGS;
  }

  for (int dev_num = 0; dev_num < kStubNumDevices; ++dev_num) {
    if (strcmp(kStubDeviceIds[dev_num], device_id) == 0) {
      stream_params->sample_rate = kStubDeviceRates[dev_num];
      stream_params->num_channels = kStubDeviceNumChans[dev_num];
      stream_params->buffer_size = kStubDeviceBufferSizes[dev_num];
      return MX_OK;
    }
  }
  return MX_ERR_NOT_FOUND;
}

int fuchsia_audio_manager_create_output_stream(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params,
    fuchsia_audio_output_stream** stream_out) {
  FXL_DCHECK(manager);
  if (!is_valid_manager(manager)) {
    return MX_ERR_BAD_HANDLE;
  }

  int dev_num = 0;
  // Use |device_id| if present, else use the default device (0)
  if (device_id && strlen(device_id)) {
    for (; dev_num < kStubNumDevices; ++dev_num) {
      if (strcmp(kStubDeviceIds[dev_num], device_id) == 0) {
        break;
      }
    }
  }
  if (dev_num == kStubNumDevices) {
    return MX_ERR_NOT_FOUND;
  }

  if (!stream_params) {
    return MX_ERR_INVALID_ARGS;
  }
  if (stream_params->num_channels < kMinNumChannels ||
      stream_params->num_channels > kMaxNumChannels ||
      stream_params->sample_rate < kMinSampleRate ||
      stream_params->sample_rate > kMaxSampleRate) {
    return MX_ERR_OUT_OF_RANGE;
  }

  if (!stream_out) {
    return MX_ERR_INVALID_ARGS;
  }

  *stream_out = new fuchsia_audio_output_stream{
      .stream_params = *stream_params,
      .delay_nsec = kStubDeviceMinDelaysNSec[dev_num],
      .received_first_pres_time = false,
      .manager = manager,
  };

  manager->streams->push_back(*stream_out);
  strncpy((*stream_out)->tag, kFuchsiaAudioOutputStreamTag, 4);

  return MX_OK;
}

int fuchsia_audio_output_stream_free(fuchsia_audio_output_stream* stream) {
  FXL_DCHECK(stream);
  if (!is_valid_stream(stream)) {
    return MX_ERR_BAD_HANDLE;
  }

  strncpy(stream->tag, kFuchsiaAudioBlankTag, 4);
  std::vector<fuchsia_audio_output_stream*>* streams = stream->manager->streams;

  for (auto str_iter = streams->begin(); str_iter != streams->end();
       ++str_iter) {
    if (*str_iter == stream) {
      streams->erase(str_iter);
      break;
    }
  }

  delete stream;
  return MX_OK;
}

int fuchsia_audio_output_stream_get_min_delay(
    fuchsia_audio_output_stream* stream,
    mx_duration_t* delay_nsec_out) {
  FXL_DCHECK(stream);
  if (!is_valid_stream(stream)) {
    return MX_ERR_BAD_HANDLE;
  }

  if (!delay_nsec_out) {
    return MX_ERR_INVALID_ARGS;
  }

  *delay_nsec_out = stream->delay_nsec;
  return MX_OK;
}

int fuchsia_audio_output_stream_write(fuchsia_audio_output_stream* stream,
                                      float* sample_buffer,
                                      int num_samples,
                                      mx_time_t pres_time) {
  FXL_DCHECK(stream);
  if (!is_valid_stream(stream)) {
    return MX_ERR_BAD_HANDLE;
  }

  if (!sample_buffer || num_samples <= 0) {
    return MX_ERR_INVALID_ARGS;
  }
  if (num_samples % stream->stream_params.num_channels) {
    return MX_ERR_INVALID_ARGS;
  }

  if (!pres_time) {
    return MX_ERR_INVALID_ARGS;
  }
  if (pres_time > FUCHSIA_AUDIO_NO_TIMESTAMP) {
    return MX_ERR_OUT_OF_RANGE;
  }
  if (pres_time == FUCHSIA_AUDIO_NO_TIMESTAMP &&
      !stream->received_first_pres_time) {
    return MX_ERR_BAD_STATE;
  }
  mx_time_t deadline = stream->delay_nsec + mx_time_get(MX_CLOCK_MONOTONIC);
  if (pres_time < deadline) {
    return MX_ERR_IO_MISSED_DEADLINE;
  }

  stream->received_first_pres_time = true;
  return MX_OK;
}
