// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_OTP_CONFIG_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_OTP_CONFIG_H_

// TODO(jsasinowski): Update this
// clang-format off
#define OTP_PAGE_NUM               37  // 40 pages in total but 3 not used
#define OTP_PAGE_SIZE              64
#define OTP_TOTAL_SIZE             (OTP_PAGE_NUM * OTP_PAGE_SIZE)
#define OTP_DUMP                   0
#define OTP_LSC_DIM                16
#define OTP_LSC_SIZE               (OTP_LSC_DIM * OTP_LSC_DIM)
#define ARM_LSC_DIM                31
#define LSC_SIZE                   (ARM_LSC_DIM * ARM_LSC_DIM)
#define OTP_WB_START               12
#define OTP_WB_SIZE                6
#define OTP_LSC_3000_R_START       18
#define OTP_LSC_3000_G_START       274
#define OTP_LSC_3000_B_START       530
#define OTP_LSC_4000_R_START       786
#define OTP_LSC_4000_G_START       1042
#define OTP_LSC_4000_B_START       1298
#define OTP_LSC_5000_R_START       1554
#define OTP_LSC_5000_G_START       1810
#define OTP_LSC_5000_B_START       2066
#define OTP_CHECKSUM_HIGH_START    2350
#define OTP_CHECKSUM_LOW_START     2351
#define OTP_PAGE_START             0x0A04
#define OTP_PAGE_SELECT            0x0A02
#define OTP_ACCESS_STATUS          0x0A01
#define OTP_READ_ENABLE            0x0A00
// clang-format on

#define MAX_INT16 (65535)

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_OTP_CONFIG_H_
