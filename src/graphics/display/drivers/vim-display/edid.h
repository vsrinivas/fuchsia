// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_VIM_DISPLAY_EDID_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_VIM_DISPLAY_EDID_H_

#include <ddk/protocol/display/controller.h>
#include <zircon/types.h>

#include "hdmitx.h"

zx_status_t get_vic(const display_mode_t* disp_timing, struct hdmi_param* p);

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_VIM_DISPLAY_EDID_H_
