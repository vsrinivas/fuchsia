// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_OTP_CONFIG_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_OTP_CONFIG_H_

// clang-format off
#define OTP_PAGE_NUM               37  // 40 pages in total but 3 not used
#define OTP_PAGE_SIZE              64
#define OTP_TOTAL_SIZE             (OTP_PAGE_NUM * OTP_PAGE_SIZE)
#define OTP_CHECKSUM_HIGH_START    2350
#define OTP_CHECKSUM_LOW_START     2351
#define OTP_PAGE_START             0x0A04
#define OTP_PAGE_SELECT            0x0A02
#define OTP_ACCESS_STATUS          0x0A01
#define OTP_READ_ENABLE            0x0A00
// clang-format on

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_OTP_CONFIG_H_
