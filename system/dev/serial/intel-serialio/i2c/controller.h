// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stdint.h>
#include <magenta/listnode.h>
#include <threads.h>

typedef struct __attribute__((packed)) intel_serialio_i2c_regs {
    uint32_t ctl;
    uint32_t tar_add;
    uint32_t _reserved0[2];
    uint32_t data_cmd;
    uint32_t ss_scl_hcnt;
    uint32_t ss_scl_lcnt;
    uint32_t fs_scl_hcnt;
    uint32_t fs_scl_lcnt;
    uint32_t _reserved1[2];
    uint32_t intr_stat;
    uint32_t intr_mask;
    uint32_t raw_intr_stat;
    uint32_t rx_tl;
    uint32_t tx_tl;
    uint32_t clr_intr;
    uint32_t clr_rx_under;
    uint32_t clr_rx_over;
    uint32_t clr_tx_over;
    uint32_t _reserved2[1];
    uint32_t clr_tx_abort;
    uint32_t _reserved3[1];
    uint32_t clr_activity;
    uint32_t clr_stop_det;
    uint32_t clr_start_det;
    uint32_t clr_gen_call;
    uint32_t i2c_en;
    uint32_t i2c_sta;
    uint32_t txflr;
    uint32_t rxflr;
    uint32_t sda_hold;
    uint32_t tx_abrt_source;
    uint32_t slv_data_nack;
    uint32_t dma_ctrl;
    uint32_t dma_tdlr;
    uint32_t dma_rdlr;
    uint32_t sda_setup;
    uint32_t ack_gen_call;
    uint32_t enable_status;
    uint32_t _reserved4[21];
    uint32_t comp_param1;
    uint32_t comp_ver;
} intel_serialio_i2c_regs;
_Static_assert(sizeof(intel_serialio_i2c_regs) <= 0x200, "bad struct");

enum {
    I2C_MAX_FAST_SPEED_HZ = 400000,
    I2C_MAX_STANDARD_SPEED_HZ = 100000,
};

enum {
    I2C_EN_ABORT = 1,
    I2C_EN_ENABLE = 0,
};

enum {
    CTL_SLAVE_DISABLE = 6,
    CTL_RESTART_ENABLE = 5,
    CTL_ADDRESSING_MODE = 4,

    CTL_ADDRESSING_MODE_7BIT = 0x0,
    CTL_ADDRESSING_MODE_10BIT = 0x1,

    CTL_SPEED = 1,
    CTL_SPEED_STANDARD = 0x1,
    CTL_SPEED_FAST = 0x2,

    CTL_MASTER_MODE = 0,
    CTL_MASTER_MODE_ENABLED = 0x1,
};

enum {
    INTR_GENERAL_CALL = 11,
    INTR_START_DETECTION = 10,
    INTR_STOP_DETECTION = 9,
    INTR_ACTIVITY = 8,
    INTR_TX_ABORT = 6,
    INTR_TX_EMPTY = 4,
    INTR_TX_OVER = 3,
    INTR_RX_FULL = 2,
    INTR_RX_OVER = 1,
    INTR_RX_UNDER = 0,
};

enum {
    TAR_ADD_WIDTH = 12,
    TAR_ADD_WIDTH_7BIT = 0x0,
    TAR_ADD_WIDTH_10BIT = 0x1,

    TAR_ADD_SPECIAL = 11,
    TAR_ADD_GC_OR_START = 10,
    TAR_ADD_IC_TAR = 0,
};

enum {
    I2C_STA_CA = 5,
    I2C_STA_RFCF = 4,
    I2C_STA_RFNE = 3,
    I2C_STA_TFCE = 2,
    I2C_STA_TFNF = 1,
    I2C_STA_ACTIVITY = 0,
};

enum {
    DATA_CMD_RESTART = 10,
    DATA_CMD_STOP = 9,

    DATA_CMD_CMD = 8,
    DATA_CMD_CMD_WRITE = 0,
    DATA_CMD_CMD_READ = 1,

    DATA_CMD_DAT = 0,
};

typedef struct intel_serialio_i2c_device {
    mx_device_t* mxdev;
    mx_device_t* pcidev;

    intel_serialio_i2c_regs* regs;
    volatile uint32_t* soft_reset;

    uint64_t regs_size;
    mx_handle_t regs_handle;

    thrd_t irq_thread;
    mx_handle_t irq_handle;
    mx_handle_t event_handle;

    uint32_t controller_freq;
    uint32_t bus_freq;

    struct list_node slave_list;

    mtx_t mutex;
    mtx_t irq_mask_mutex;
} intel_serialio_i2c_device_t;

mx_status_t intel_serialio_i2c_reset_controller(
    intel_serialio_i2c_device_t* controller);

mx_status_t intel_serialio_i2c_wait_for_rx_full(
    intel_serialio_i2c_device_t* controller,
    mx_time_t deadline);
mx_status_t intel_serialio_i2c_wait_for_tx_empty(
    intel_serialio_i2c_device_t* controller,
    mx_time_t deadline);
mx_status_t intel_serialio_i2c_wait_for_stop_detect(
    intel_serialio_i2c_device_t* controller,
    mx_time_t deadline);

// Acts on the DATA_CMD register, and clear
// interrupt masks as appropriate
mx_status_t intel_serialio_i2c_issue_rx(
    intel_serialio_i2c_device_t* controller,
    uint32_t data_cmd);
mx_status_t intel_serialio_i2c_read_rx(
    intel_serialio_i2c_device_t* controller,
    uint8_t* data);
mx_status_t intel_serialio_i2c_issue_tx(
    intel_serialio_i2c_device_t* controller,
    uint32_t data_cmd);
mx_status_t intel_serialio_i2c_clear_stop_detect(
    intel_serialio_i2c_device_t* controller);

void intel_serialio_i2c_get_rx_fifo_threshold(
    intel_serialio_i2c_device_t* controller,
    uint32_t* threshold);
mx_status_t intel_serialio_i2c_set_rx_fifo_threshold(
    intel_serialio_i2c_device_t* controller,
    uint32_t threshold);

void intel_serialio_i2c_get_tx_fifo_threshold(
    intel_serialio_i2c_device_t* controller,
    uint32_t* threshold);
mx_status_t intel_serialio_i2c_set_tx_fifo_threshold(
    intel_serialio_i2c_device_t* controller,
    uint32_t threshold);
