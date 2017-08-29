// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <string>

#include <magenta/errors.h>
#include <magenta/syscalls.h>

#include "lib/ftl/logging.h"

#include "apps/media/lib/c/audio.h"

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
};
struct _fuchsia_audio_manager {
  char tag[kTagSize];
};

namespace {
// Soft limits only - can be expanded if needed (except kMinNumChannels).
constexpr int kMinSampleRate = 8000;
constexpr int kMaxSampleRate = 96000;
constexpr int kMinNumChannels = 1;
constexpr int kMaxNumChannels = 2;

// Currently this file is a stub implementation of the interface in audio.h.

// TODO(mpuryear): Convert the stub implementation to a full implementation that
// connects to the rest of the audio subsystem, including enumerating audio
// playback devices, connecting to selected devices, and playing audio to said
// devices.

constexpr int kStubNumDevices = 2;

static const char* kStubDeviceIds[kStubNumDevices] = {"dummy1", "dummy2"};
static const char* kStubDeviceNames[kStubNumDevices] = {"Dummy Audio Output 1",
                                                        "Dummy Audio Output 2"};
constexpr int kStubDeviceRates[kStubNumDevices] = {44100, 48000};
constexpr int kStubDeviceNumChans[kStubNumDevices] = {2, 1};
constexpr int kStubDeviceBufferSizes[kStubNumDevices] = {1024, 3000};
constexpr mx_duration_t kStubDeviceMinDelaysNSec[kStubNumDevices] = {102400000,
                                                                     1000000};
// End of stub-related placeholder values

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
  strncpy(manager->tag, kFuchsiaAudioManagerTag, 4);

  // TODO(mpuryear): initialize whatever container tracks this mgr's streams

  return manager;
}

int fuchsia_audio_manager_free(fuchsia_audio_manager* manager) {
  FTL_DCHECK(manager);
  if (!is_valid_manager(manager)) {
    return MX_ERR_BAD_HANDLE;
  }

  // TODO(mpuryear): When manager is freed, free its associated streams

  strncpy(manager->tag, kFuchsiaAudioBlankTag, 4);
  delete manager;
  return MX_OK;
}

int fuchsia_audio_manager_get_output_devices(
    fuchsia_audio_manager* manager,
    fuchsia_audio_device_description* buffer,
    int num_device_descriptions) {
  FTL_DCHECK(manager);
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
  FTL_DCHECK(manager);
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
  FTL_DCHECK(manager);
  if (!is_valid_manager(manager)) {
    return MX_ERR_BAD_HANDLE;
  }

  int dev_num = 0;
  // Open default device when |device_id| isn't specified.
  if (device_id && strlen(device_id)) {
    for (dev_num = 0; dev_num < kStubNumDevices; ++dev_num) {
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
  };
  strncpy((*stream_out)->tag, kFuchsiaAudioOutputStreamTag, 4);

  // TODO(mpuryear): Store stream in stream->manager's container of streams

  return MX_OK;
}

int fuchsia_audio_output_stream_free(fuchsia_audio_output_stream* stream) {
  FTL_DCHECK(stream);
  if (!is_valid_stream(stream)) {
    return MX_ERR_BAD_HANDLE;
  }

  // TODO(mpuryear): Remove stream from stream->manager's container of streams

  strncpy(stream->tag, kFuchsiaAudioBlankTag, 4);

  delete stream;
  return MX_OK;
}

int fuchsia_audio_output_stream_get_min_delay(
    fuchsia_audio_output_stream* stream,
    mx_duration_t* delay_nsec_out) {
  FTL_DCHECK(stream);
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
  FTL_DCHECK(stream);
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
  mx_time_t horizon = stream->delay_nsec + mx_time_get(MX_CLOCK_MONOTONIC);
  if (pres_time < horizon) {
    return MX_ERR_IO_MISSED_DEADLINE;
  }

  stream->received_first_pres_time = true;
  return MX_OK;
}
