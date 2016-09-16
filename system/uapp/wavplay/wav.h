// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

typedef struct {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t wave_id;
} __attribute__ ((packed)) riff_wave_header;

typedef struct {
    uint32_t id;
    uint32_t sz;
} __attribute__ ((packed)) chunk_header;

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} __attribute__ ((packed)) chunk_fmt;
