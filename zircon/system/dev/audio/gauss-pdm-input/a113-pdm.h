// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include "a113-audio-device.h"

__BEGIN_CDECLS;

void a113_pdm_dump_registers(a113_audio_device_t* audio_device);
void a113_pdm_ctrl(a113_audio_device_t* actrl, int bitdepth);
void a113_pdm_arb_config(a113_audio_device_t* actrl);

// This for now takes no params, and sets the filters to hardcoded
// configuration.
void a113_pdm_filter_ctrl(a113_audio_device_t* audio_device);
void a113_pdm_fifo_reset(a113_audio_device_t* audio_device);
void a113_pdm_enable(a113_audio_device_t* audio_device, int is_enable);
__END_CDECLS;
