// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <platform/msm8998.h>

#define GICBASE(n)  (MSM8998_GIC_BASE_VIRT)
#define GICD_OFFSET (0x000000)
#define GICR_OFFSET (0x100000)
