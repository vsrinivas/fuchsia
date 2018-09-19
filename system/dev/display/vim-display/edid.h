// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/display/controller.h>
#include <zircon/types.h>

#include "hdmitx.h"

zx_status_t get_vic(const display_mode_t* disp_timing, struct hdmi_param* p);
