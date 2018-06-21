// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <hw/sdio.h>

#include <ddk/device.h>

#define SDIO_CARD_MULTI_BLOCK    (1 << 0)
#define SDIO_CARD_SRW            (1 << 1)
#define SDIO_CARD_DIRECT_COMMAND (1 << 2)
#define SDIO_CARD_SUSPEND_RESUME (1 << 3)
#define SDIO_CARD_LOW_SPEED      (1 << 4)
#define SDIO_CARD_HIGH_SPEED     (1 << 5)
#define SDIO_CARD_HIGH_POWER     (1 << 6)
#define SDIO_CARD_4BIT_BUS       (1 << 7)

typedef struct sdio_func_tuple {
    uint8_t t_code;
    uint8_t t_body_size;
    uint8_t *t_body;
} sdio_func_tuple_t;

typedef struct sdio_func_info {
    uint32_t manufacturer_id;
    uint32_t product_id;
    uint32_t max_blk_size;
    uint16_t cur_blk_size;
    uint32_t max_tran_speed;
    uint8_t  fn_intf_code;
    bool enabled;
    bool intr_enabled;
} sdio_func_info_t;

typedef struct sdio_device_info {
    uint32_t  num_funcs;                 // number of sdio funcs
    uint32_t  sdio_vsn;
    uint32_t  cccr_vsn;
    uint32_t  caps;
    sdio_func_info_t funcs[SDIO_MAX_FUNCS];
} sdio_device_info_t;

static inline void update_bits(uint32_t *x, uint32_t mask, uint32_t loc, uint32_t val) {
    *x &= ~mask;
    *x |= ((val << loc) & mask);
}

static inline uint32_t get_bits(uint32_t x, uint32_t mask, uint32_t loc) {
    return (x & mask) >> loc;
}

static inline bool get_bit(uint32_t x, uint32_t mask) {
    return (x & mask) ? 1 : 0;
}

static inline void update_bits_u8(uint8_t *x, uint8_t mask, uint8_t loc, uint8_t val) {
    *x &= ~mask;
    *x |= ((val << loc) & mask);
}

static inline uint32_t get_bits_u8(uint8_t x, uint8_t mask, uint8_t loc) {
    return (x & mask) >> loc;
}

static inline bool get_bit_u8(uint8_t x, uint8_t mask) {
    return (x & mask) ? 1 : 0;
}

