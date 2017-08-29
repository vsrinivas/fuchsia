// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#pragma once

#include <magenta/types.h>
#include <stdint.h>

#define MEDIA_CLIENT_EXPORT __attribute__((__visibility__("default")))

#include <magenta/compiler.h>
__BEGIN_CDECLS

const size_t FUCHSIA_AUDIO_MAX_DEVICE_NAME_LENGTH = 256;

// mx_time_t is a uint64, but we limit it to only the bottom half of the range,
// to smoothly interface with int64 timestamps elsewhere in the system. Hence
// the highest possible timestamp (reserved by the system to signify 'whatever
// timestamp will give me gapless playback') is the max SIGNED value.
const mx_time_t FUCHSIA_AUDIO_NO_TIMESTAMP = INT64_MAX;

struct _fuchsia_audio_manager;
typedef struct _fuchsia_audio_manager fuchsia_audio_manager;

typedef struct {
  char name[FUCHSIA_AUDIO_MAX_DEVICE_NAME_LENGTH];
  char id[FUCHSIA_AUDIO_MAX_DEVICE_NAME_LENGTH];
} fuchsia_audio_device_description;

typedef struct {
  int sample_rate;
  int num_channels;
  int buffer_size;
} fuchsia_audio_parameters;

struct _fuchsia_audio_output_stream;
typedef struct _fuchsia_audio_output_stream fuchsia_audio_output_stream;

MEDIA_CLIENT_EXPORT fuchsia_audio_manager* fuchsia_audio_manager_create();

MEDIA_CLIENT_EXPORT int fuchsia_audio_manager_free(
    fuchsia_audio_manager* audio_manager);

MEDIA_CLIENT_EXPORT int fuchsia_audio_manager_get_output_devices(
    fuchsia_audio_manager* audio_manager,
    fuchsia_audio_device_description* device_desc_buffer,
    int num_device_descriptions);

MEDIA_CLIENT_EXPORT int
fuchsia_audio_manager_get_output_device_default_parameters(
    fuchsia_audio_manager* audio_manager,
    char* device_id,
    fuchsia_audio_parameters* params_out);

MEDIA_CLIENT_EXPORT int fuchsia_audio_manager_create_output_stream(
    fuchsia_audio_manager* audio_manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params,
    fuchsia_audio_output_stream** stream_out);

MEDIA_CLIENT_EXPORT int fuchsia_audio_output_stream_free(
    fuchsia_audio_output_stream* stream);

MEDIA_CLIENT_EXPORT int fuchsia_audio_output_stream_get_min_delay(
    fuchsia_audio_output_stream* stream,
    mx_duration_t* delay_nsec_out);

MEDIA_CLIENT_EXPORT int fuchsia_audio_output_stream_write(
    fuchsia_audio_output_stream* stream,
    float* sample_buffer,
    int num_samples,
    mx_time_t pres_time);

__END_CDECLS
