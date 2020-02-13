// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_HIKEY_DISPLAY_COMMON_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_HIKEY_DISPLAY_COMMON_H_

#include <ddk/debug.h>

namespace hi_display {

// define for DW-DSI BIST and ADV7533-Bridge Test
#define DW_DSI_TEST_ENABLE

#define TRACE zxlogf(INFO, "%s %d\n", __FUNCTION__, __LINE__);

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

static constexpr uint8_t kAdv7533FixedRegs[] = {0x16, 0x20, 0x9a, 0xe0, 0xba, 0x70,
                                                0xde, 0x82, 0xe4, 0x40, 0xe5, 0x80};

static constexpr uint8_t kAdv7533CecFixedRegs[] = {0x15, 0xd0, 0x17, 0xd0, 0x24,
                                                   0x20, 0x57, 0x11, 0x05, 0xc8};

// TODO: Update with hardware specific values
constexpr uint64_t kDisplayId = 1;
constexpr uint32_t kRefreshRateFps = 60;
constexpr uint32_t kMaxLayer = 1;

extern uint32_t kWidth;
extern uint32_t kHeight;
extern uint8_t edid_buf_[256];

}  // namespace hi_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_HIKEY_DISPLAY_COMMON_H_
