// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/c/audio.h"
#include "lib/fxl/logging.h"

#include "audio_output_device.h"
#include "audio_output_manager.h"
#include "audio_output_stream.h"

struct _fuchsia_audio_output_stream {};
struct _fuchsia_audio_manager {};

// Public API functions
fuchsia_audio_manager* fuchsia_audio_manager_create() {
  media_client::AudioOutputManager* manager =
      new media_client::AudioOutputManager();
  return reinterpret_cast<fuchsia_audio_manager*>(manager);
}

void fuchsia_audio_manager_free(fuchsia_audio_manager* manager) {
  FXL_DCHECK(manager);
  auto manager_obj =
      reinterpret_cast<media_client::AudioOutputManager*>(manager);
  delete manager_obj;
}

int fuchsia_audio_manager_get_output_devices(
    fuchsia_audio_manager* manager,
    fuchsia_audio_device_description* buffer,
    int num_device_descriptions) {
  FXL_DCHECK(manager);
  auto manager_obj =
      reinterpret_cast<media_client::AudioOutputManager*>(manager);
  return manager_obj->GetOutputDevices(buffer, num_device_descriptions);
}

int fuchsia_audio_manager_get_output_device_default_parameters(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params) {
  FXL_DCHECK(manager);
  auto manager_obj =
      reinterpret_cast<media_client::AudioOutputManager*>(manager);
  return manager_obj->GetOutputDeviceDefaultParameters(device_id,
                                                       stream_params);
}

int fuchsia_audio_manager_create_output_stream(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params,
    fuchsia_audio_output_stream** stream_out) {
  FXL_DCHECK(manager);
  FXL_DCHECK(stream_out);

  auto manager_obj =
      reinterpret_cast<media_client::AudioOutputManager*>(manager);

  media_client::AudioOutputStream* stream = nullptr;
  int status =
      manager_obj->CreateOutputStream(device_id, stream_params, &stream);

  if (status == ZX_OK) {
    *stream_out = reinterpret_cast<fuchsia_audio_output_stream*>(stream);
  }

  return status;
}

int fuchsia_audio_output_stream_free(fuchsia_audio_output_stream* stream) {
  FXL_DCHECK(stream);
  auto stream_obj = reinterpret_cast<media_client::AudioOutputStream*>(stream);
  return stream_obj->Free();
}

int fuchsia_audio_output_stream_get_min_delay(
    fuchsia_audio_output_stream* stream,
    zx_duration_t* delay_nsec_out) {
  FXL_DCHECK(stream);
  auto stream_obj = reinterpret_cast<media_client::AudioOutputStream*>(stream);
  return stream_obj->GetMinDelay(delay_nsec_out);
}

int fuchsia_audio_output_stream_write(fuchsia_audio_output_stream* stream,
                                      float* sample_buffer,
                                      int num_samples,
                                      zx_time_t pres_time) {
  FXL_DCHECK(stream);
  auto stream_obj = reinterpret_cast<media_client::AudioOutputStream*>(stream);
  return stream_obj->Write(sample_buffer, num_samples, pres_time);
}
