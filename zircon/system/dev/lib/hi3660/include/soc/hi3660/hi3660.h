// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/mmio-buffer.h>
#include <ddk/protocol/gpioimpl.h>
#include <zircon/listnode.h>

typedef struct {
    list_node_t gpios;
    gpio_impl_protocol_t gpio;
    mmio_buffer_t usb3otg_bc;
    mmio_buffer_t peri_crg;
    mmio_buffer_t iomcu;
    mmio_buffer_t pctrl;
    mmio_buffer_t iomg_pmx4;
    mmio_buffer_t iocfg_pmx9;
    mmio_buffer_t pmu_ssio;
    mmio_buffer_t ufs_sctrl;
} hi3660_t;

zx_status_t hi3660_init(zx_handle_t resource, hi3660_t** out);
zx_status_t hi3660_get_protocol(hi3660_t* hi3660, uint32_t proto_id, void* out);
void hi3660_release(hi3660_t* hi3660);

// hi3660-gpios.c
zx_status_t hi3660_gpio_init(hi3660_t* hi3660);
void hi3660_gpio_release(hi3660_t* hi3660);

// hi3660-usb.c
zx_status_t hi3660_usb_init(hi3660_t* hi3660);

// hi3660-i2c.c
zx_status_t hi3660_i2c1_init(hi3660_t* hi3660);
zx_status_t hi3660_i2c_pinmux(hi3660_t* hi3660);
zx_status_t hi3660_enable_ldo(hi3660_t* hi3660);

// hi3660-dsi.c
zx_status_t hi3660_dsi_init(hi3660_t* hi3660);

// hi3660-ufs.c
zx_status_t hi3660_ufs_init(hi3660_t* hi3660);
