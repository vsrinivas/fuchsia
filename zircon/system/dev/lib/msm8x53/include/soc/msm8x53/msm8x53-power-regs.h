// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <soc/msm8x53/msm8x53-hw.h>
#include <zircon/types.h>

//PMIC WRAP MMIOS

static constexpr uint8_t kPmicArbCoreMmioIndex = 0;
static constexpr uint8_t kPmicArbChnlsMmioIndex = 1;
static constexpr uint8_t kPmicArbObsrvrMmioIndex = 2;
static constexpr uint8_t kPmicArbIntrMmioIndex = 3;
static constexpr uint8_t kPmicArbCnfgMmioIndex = 4;
