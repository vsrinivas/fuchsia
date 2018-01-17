// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/i2c.h>
#include <sync/completion.h>
#include <zircon/listnode.h>

#include <threads.h>

#define I2C_ERROR_SIGNAL ZX_USER_SIGNAL_0
#define I2C_TXN_COMPLETE_SIGNAL ZX_USER_SIGNAL_1

#define AML_I2C_CONTROL_REG_START      (uint32_t)(1 << 0)
#define AML_I2C_CONTROL_REG_ACK_IGNORE (uint32_t)(1 << 1)
#define AML_I2C_CONTROL_REG_STATUS     (uint32_t)(1 << 2)
#define AML_I2C_CONTROL_REG_ERR        (uint32_t)(1 << 3)

#define AML_I2C_MAX_TRANSFER 256

typedef struct aml_i2c_dev aml_i2c_dev_t;


typedef enum {
    TOKEN_END,
    TOKEN_START,
    TOKEN_SLAVE_ADDR_WR,
    TOKEN_SLAVE_ADDR_RD,
    TOKEN_DATA,
    TOKEN_DATA_LAST,
    TOKEN_STOP
} aml_i2c_token_t;

typedef volatile struct {
    uint32_t    control;
    uint32_t    slave_addr;
    uint32_t    token_list_0;
    uint32_t    token_list_1;
    uint32_t    token_wdata_0;
    uint32_t    token_wdata_1;
    uint32_t    token_rdata_0;
    uint32_t    token_rdata_1;
} aml_i2c_regs_t;

typedef struct {
    list_node_t    node;
    uint32_t       slave_addr;
    uint32_t       addr_bits;
    aml_i2c_dev_t  *dev;
} aml_i2c_connection_t;

/*
    We have separate tx and rx buffs since a common need with i2c
    is the ability to do a write,read sequence without having another
    transaction on the bus in between the write/read.
*/
typedef struct aml_i2c_txn aml_i2c_txn_t;

typedef struct aml_i2c_txn {
    list_node_t            node;
    uint8_t                tx_buff[AML_I2C_MAX_TRANSFER];
    uint8_t                rx_buff[AML_I2C_MAX_TRANSFER];
    uint32_t               tx_len;
    uint32_t               rx_len;
    aml_i2c_connection_t   *conn;
    i2c_complete_cb        cb;
    void*                  cookie;
} aml_i2c_txn_t;

struct aml_i2c_dev {
    zx_handle_t    irq;
    zx_handle_t    event;
    io_buffer_t    regs_iobuff;
    aml_i2c_regs_t *virt_regs;
    zx_duration_t  timeout;

    uint32_t       bitrate;
    list_node_t    connections;
    list_node_t    txn_list;
    list_node_t    free_txn_list;
    completion_t   txn_active;
    mtx_t          conn_mutex;
    mtx_t          txn_mutex;
};
