// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_GAUSS_GAUSS_H_
#define SRC_DEVICES_BOARD_DRIVERS_GAUSS_GAUSS_H_

#include <threads.h>

#include <ddk/mmio-buffer.h>
#include <ddk/protocol/gpioimpl.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-a113/a113-clocks.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  AML_I2C_A,
  AML_I2C_B,
  AML_I2C_C,
  AML_I2C_D,
};

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_AUDIO_IN,
  BTI_AUDIO_OUT,
  BTI_USB_XHCI,
  BTI_AML_RAW_NAND,
  BTI_SYSMEM,
};

typedef struct {
  zx_device_t* parent;
  pbus_protocol_t pbus;
  gpio_impl_protocol_t gpio;
  iommu_protocol_t iommu;
  zx_handle_t bti_handle;
  mmio_buffer_t usb_phy;
  zx_handle_t usb_phy_irq_handle;
  thrd_t phy_irq_thread;
  a113_clk_dev_t* clocks;
} gauss_bus_t;

// gauss-sysmem.c
zx_status_t gauss_sysmem_init(gauss_bus_t* bus);

// gauss-audio.c
zx_status_t gauss_audio_init(gauss_bus_t* bus);

// gauss-gpio.c
zx_status_t gauss_gpio_init(gauss_bus_t* bus);

// gauss-i2c.c
zx_status_t gauss_i2c_init(gauss_bus_t* bus);

// gauss-usb.c
zx_status_t gauss_usb_init(gauss_bus_t* bus);

// gauss-clk.c
zx_status_t gauss_clk_init(gauss_bus_t* bus);

// gauss-pcie.cc
zx_status_t gauss_pcie_init(gauss_bus_t* bus);

// gauss-raw_nand.c
zx_status_t gauss_raw_nand_init(gauss_bus_t* bus);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SRC_DEVICES_BOARD_DRIVERS_GAUSS_GAUSS_H_
