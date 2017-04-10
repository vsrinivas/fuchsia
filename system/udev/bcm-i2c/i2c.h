// Copyright 2017 The Fuchsia Authors
// All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

typedef volatile struct {

    uint32_t    control;
    uint32_t    status;
    uint32_t    dlen;
    uint32_t    slave_addr;
    uint32_t    fifo;
    uint32_t    clk_div;
    uint32_t    data_delay;
    uint32_t    clk_stretch;

} bcm_i2c_regs_t;

#define     BCM_BSC_FIFO_SIZE           (uint32_t)16

#define     BCM_BSC_STATUS_DONE         (uint32_t)(1 << 1)
#define     BCM_BSC_STATUS_ERR          (uint32_t)(1 << 8)

#define     BCM_BSC_CONTROL_READ        (uint32_t)0x00000001
#define     BCM_BSC_CONTROL_ENABLE      (uint32_t)(1 << 15)
#define     BCM_BSC_CONTROL_FIFO_CLEAR  (uint32_t)(0x03 << 4)
#define     BCM_BSC_CONTROL_START       (uint32_t)(1 << 7)

#define     BCM_BSC_CLK_DIV_100K        (uint32_t)(2500)

