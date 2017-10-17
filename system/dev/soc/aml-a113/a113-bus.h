// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-bus.h>
#include <ddk/io-buffer.h>

typedef struct {
    platform_bus_protocol_t pbus;
    io_buffer_t periphs_reg;        // PMux/GPIO
    io_buffer_t periphs_ao_reg;     // PMux/GPIO for AO domain
    // more coming soon
} a113_bus_t;

// a113-audio.c
zx_status_t a113_audio_init(a113_bus_t* bus);

// a113-pinmux.c
zx_status_t a113_config_pinmux(void* ctx, const uint32_t pin, const uint32_t fn);
zx_status_t a113_init_pinmux(a113_bus_t* bus);
