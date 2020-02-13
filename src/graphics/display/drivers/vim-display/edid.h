// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_EDID_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_EDID_H_

#include <zircon/types.h>

#include <ddk/protocol/display/controller.h>

#include "hdmitx.h"

zx_status_t get_vic(const display_mode_t* disp_timing, struct hdmi_param* p);

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_EDID_H_
