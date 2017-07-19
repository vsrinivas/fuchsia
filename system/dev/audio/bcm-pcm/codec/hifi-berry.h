// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <magenta/device/audio.h>
#include <magenta/types.h>

// clang-format off
#define HIFIBERRY_STATE_SHUTDOWN        (uint32_t)( 0 )
#define HIFIBERRY_STATE_INITIALIZED     (uint32_t)( 1 << 0 )
// clang-format on

bool hifiberry_is_valid_mode(audio_stream_cmd_set_format_req_t req);

mx_status_t hifiberry_init(void);
mx_status_t hifiberry_start(void);
mx_status_t hifiberry_stop(void);
mx_status_t hifiberry_release(void);
