// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>

typedef struct clk_freq_info {
    char clk_name[30];
    uint32_t clk_freq;
} clk_freq_info_t;

// Measure clock frequency.
#define IOCTL_CLK_MEASURE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CLK, 1)

// size_t ioctl_clk_measure(int fd, uint32_t index, clk_freq_info_t* clk_info);
IOCTL_WRAPPER_INOUT(ioctl_clk_measure_and_dump, IOCTL_CLK_MEASURE, uint32_t, clk_freq_info_t);
