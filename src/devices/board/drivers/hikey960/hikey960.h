// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_HIKEY960_HIKEY960_H_
#define SRC_DEVICES_BOARD_DRIVERS_HIKEY960_HIKEY960_H_

#include <zircon/listnode.h>

#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/gpioimpl.h>
#include <ddk/protocol/platform/bus.h>

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_USB_DWC3,
  BTI_DSI,
  BTI_MALI,
  BTI_UFS_DWC3,
  BTI_SYSMEM,
};

typedef struct {
  pbus_protocol_t pbus;
  zx_device_t* parent;
  zx_handle_t bti_handle;

  mmio_buffer_t usb3otg_bc;
  mmio_buffer_t peri_crg;
  mmio_buffer_t iomcu;
  mmio_buffer_t pctrl;
  mmio_buffer_t iomg_pmx4;
  mmio_buffer_t iocfg_pmx9;
  mmio_buffer_t pmu_ssio;
  mmio_buffer_t ufs_sctrl;
} hikey960_t;

// hikey960-devices.c
zx_status_t hikey960_add_devices(hikey960_t* bus);

// hikey960-sysmem.c
zx_status_t hikey960_sysmem_init(hikey960_t* hikey);

// hikey960-gpio.c
zx_status_t hikey960_gpio_init(hikey960_t* bus);

// hikey960-i2c.c
zx_status_t hikey960_i2c1_init(hikey960_t* hikey);
zx_status_t hikey960_i2c_pinmux(hikey960_t* hikey);
zx_status_t hikey960_enable_ldo(hikey960_t* hikey);
zx_status_t hikey960_i2c_init(hikey960_t* bus);

// hikey960-usb.c
zx_status_t hikey960_usb_init(hikey960_t* hikey);

// hikey960-dsi.c
zx_status_t hikey960_dsi_init(hikey960_t* hikey);

// hikey960-ufs.c
zx_status_t hikey960_ufs_init(hikey960_t* hikey);

#endif  // SRC_DEVICES_BOARD_DRIVERS_HIKEY960_HIKEY960_H_
