// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef struct {
    uint32_t dma_burst_len;
    uint32_t usb_turnaround_time;
    uint32_t rx_fifo_size;
    uint32_t nptx_fifo_size;
} dwc2_metadata_t;

// Values for dma_burst_len
enum {
    DWC2_DMA_BURST_SINGLE = 0,
    DWC2_DMA_BURST_INCR = 1,
    DWC2_DMA_BURST_INCR4 = 3,
    DWC2_DMA_BURST_INCR8 = 5,
    DWC2_DMA_BURST_INCR16 = 7,
};
