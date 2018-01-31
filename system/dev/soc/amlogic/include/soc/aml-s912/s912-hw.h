// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// DMC registers
#define DMC_REG_BASE        0xc8838000

#define DMC_CAV_LUT_DATAL           (0x12 << 2)
#define DMC_CAV_LUT_DATAH           (0x13 << 2)
#define DC_CAV_LUT_ADDR             (0x14 << 2)

#define DC_CAV_LUT_ADDR_INDEX_MASK  0x7
#define DC_CAV_LUT_ADDR_RD_EN       (1 << 8)
#define DC_CAV_LUT_ADDR_WR_EN       (2 << 8)

