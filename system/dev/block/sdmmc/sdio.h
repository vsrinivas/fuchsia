// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <ddk/protocol/sdio.h>

typedef struct sdio_func_tuple {
    uint8_t t_code;
    uint8_t t_body_size;
    uint8_t *t_body;
} sdio_func_tuple_t;

typedef struct sdio_function {
    sdio_func_hw_info_t hw_info;
    uint16_t cur_blk_size;
    bool enabled;
    bool intr_enabled;
} sdio_function_t;

typedef struct sdio_device {
    sdio_device_hw_info_t hw_info;
    sdio_function_t funcs[SDIO_MAX_FUNCS];
} sdio_device_t;


static inline bool sdio_is_uhs_supported(uint32_t hw_caps) {
    return ((hw_caps & SDIO_CARD_UHS_SDR50) || (hw_caps & SDIO_CARD_UHS_SDR104) ||
           (hw_caps & SDIO_CARD_UHS_DDR50));
}

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

