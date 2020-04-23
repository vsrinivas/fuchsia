// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_DWC2_METADATA_H_
#define SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_DWC2_METADATA_H_

#include <stdint.h>

typedef struct {
  // dma_burst_len for GAHBCFG register.
  uint32_t dma_burst_len;

  // usb_turnaround_time for GUSBCFG register.
  uint32_t usb_turnaround_time;

  // RX fifo size for GRXFSIZ register, in 4 byte word units.
  uint32_t rx_fifo_size;

  // non-periodic TX fifo size for GNPTXFSIZ register, in 4 byte word units.
  // This is used only for endpoint zero.
  uint32_t nptx_fifo_size;

  // TX fifo size for remaining IN endpoints, for DTXFSIZn registers, in 4 byte work units.
  // These sizes should match max packet sizes for our IN endpoints.
  uint32_t tx_fifo_sizes[15];
} dwc2_metadata_t;

// Values for dma_burst_len
enum {
  DWC2_DMA_BURST_SINGLE = 0,
  DWC2_DMA_BURST_INCR = 1,
  DWC2_DMA_BURST_INCR4 = 3,
  DWC2_DMA_BURST_INCR8 = 5,
  DWC2_DMA_BURST_INCR16 = 7,
};

#endif  // SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_DWC2_METADATA_H_
