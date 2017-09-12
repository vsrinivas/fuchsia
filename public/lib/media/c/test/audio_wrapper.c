// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include audio.h in .c file, to ensure it can be compiled and used as C.
#include "garnet/public/lib/media/c/audio.h"

fuchsia_audio_manager* audio_manager_create() {
  return fuchsia_audio_manager_create();
}

void audio_manager_free(fuchsia_audio_manager* manager) {
  fuchsia_audio_manager_free(manager);
}

int audio_manager_get_output_devices(fuchsia_audio_manager* manager,
                                     fuchsia_audio_device_description* buffer,
                                     int num_device_descriptions) {
  return fuchsia_audio_manager_get_output_devices(manager, buffer,
                                                  num_device_descriptions);
}

int audio_manager_get_output_device_default_parameters(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* params_out) {
  return fuchsia_audio_manager_get_output_device_default_parameters(
      manager, device_id, params_out);
}

int audio_manager_create_output_stream(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params,
    fuchsia_audio_output_stream** stream_out) {
  return fuchsia_audio_manager_create_output_stream(manager, device_id,
                                                    stream_params, stream_out);
}

int audio_output_stream_free(fuchsia_audio_output_stream* stream) {
  return fuchsia_audio_output_stream_free(stream);
}

int audio_output_stream_get_min_delay(fuchsia_audio_output_stream* stream,
                                      zx_duration_t* delay_nsec_out) {
  return fuchsia_audio_output_stream_get_min_delay(stream, delay_nsec_out);
}

int audio_output_stream_write(fuchsia_audio_output_stream* stream,
                              float* sample_buffer,
                              int num_samples,
                              zx_time_t pres_time) {
  return fuchsia_audio_output_stream_write(stream, sample_buffer, num_samples,
                                           pres_time);
}
