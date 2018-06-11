// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>

// Sets the GPU  clock freq. source
#define IOCTL_GPU_SET_CLK_FREQ_SOURCE \
        IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_GPU, 1)

// ssize_t ioctl_gpu_set_freq_source(int fd, uint32_t clk_source)
IOCTL_WRAPPER_IN(ioctl_gpu_set_clk_freq_source, IOCTL_GPU_SET_CLK_FREQ_SOURCE, int32_t);
