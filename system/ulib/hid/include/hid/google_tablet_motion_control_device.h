// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

typedef struct google_tablet_motion_control_device {
  // 0 if laptop mode, 1 if tablet mode.
  uint8_t is_in_tablet_mode;
} __PACKED google_tablet_motion_control_device_t;

bool is_google_tablet_motion_control_device_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_google_tablet_motion_control_device(int fd);

__END_CDECLS
