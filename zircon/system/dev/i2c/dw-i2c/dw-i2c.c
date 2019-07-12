// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2cimpl.h>
#include <ddk/protocol/platform/device.h>
#include <hw/reg.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/sync/completion.h>
#include <zircon/assert.h>
#include <zircon/process.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "dw-i2c-regs.h"

#define I2C_AS370_DW_TEST 0

typedef struct {
  zx_handle_t irq_handle;
  zx_handle_t event_handle;
  mmio_buffer_t regs_iobuff;
  zx_duration_t timeout;

  uint32_t tx_fifo_depth;
  uint32_t rx_fifo_depth;

  mtx_t transact_lock; /* used to serialize transactions */
  mtx_t ops_lock;      /* used to set ops for irq thread */

  /* guarded by ops lock */
  i2c_impl_op_t* ops;
  size_t ops_count;
  uint32_t rx_op_idx;
  uint32_t tx_op_idx;
  uint32_t rx_done_len;
  uint32_t tx_done_len;
  uint32_t rx_pending;
  bool send_restart;
} i2c_dw_dev_t;

typedef struct {
  pdev_protocol_t pdev;
  zx_device_t* zxdev;
  i2c_dw_dev_t* i2c_devs;
  size_t i2c_dev_count;
} i2c_dw_t;

static zx_status_t i2c_dw_set_slave_addr(i2c_dw_dev_t* dev, uint16_t addr);

zx_status_t i2c_dw_dumpstate(i2c_dw_dev_t* dev) {
  zxlogf(INFO, "########################\n");
  zxlogf(INFO, "%s\n", __FUNCTION__);
  zxlogf(INFO, "########################\n");
  zxlogf(INFO, "DW_I2C_ENABLE_STATUS = \t0x%x\n", I2C_DW_READ32(DW_I2C_ENABLE_STATUS));
  zxlogf(INFO, "DW_I2C_ENABLE = \t0x%x\n", I2C_DW_READ32(DW_I2C_ENABLE));
  zxlogf(INFO, "DW_I2C_CON = \t0x%x\n", I2C_DW_READ32(DW_I2C_CON));
  zxlogf(INFO, "DW_I2C_TAR = \t0x%x\n", I2C_DW_READ32(DW_I2C_TAR));
  zxlogf(INFO, "DW_I2C_HS_MADDR = \t0x%x\n", I2C_DW_READ32(DW_I2C_HS_MADDR));
  zxlogf(INFO, "DW_I2C_SS_SCL_HCNT = \t0x%x\n", I2C_DW_READ32(DW_I2C_SS_SCL_HCNT));
  zxlogf(INFO, "DW_I2C_SS_SCL_LCNT = \t0x%x\n", I2C_DW_READ32(DW_I2C_SS_SCL_LCNT));
  zxlogf(INFO, "DW_I2C_FS_SCL_HCNT = \t0x%x\n", I2C_DW_READ32(DW_I2C_FS_SCL_HCNT));
  zxlogf(INFO, "DW_I2C_FS_SCL_LCNT = \t0x%x\n", I2C_DW_READ32(DW_I2C_FS_SCL_LCNT));
  zxlogf(INFO, "DW_I2C_INTR_MASK = \t0x%x\n", I2C_DW_READ32(DW_I2C_INTR_MASK));
  zxlogf(INFO, "DW_I2C_RAW_INTR_STAT = \t0x%x\n", I2C_DW_READ32(DW_I2C_RAW_INTR_STAT));
  zxlogf(INFO, "DW_I2C_RX_TL = \t0x%x\n", I2C_DW_READ32(DW_I2C_RX_TL));
  zxlogf(INFO, "DW_I2C_TX_TL = \t0x%x\n", I2C_DW_READ32(DW_I2C_TX_TL));
  zxlogf(INFO, "DW_I2C_STATUS = \t0x%x\n", I2C_DW_READ32(DW_I2C_STATUS));
  zxlogf(INFO, "DW_I2C_TXFLR = \t0x%x\n", I2C_DW_READ32(DW_I2C_TXFLR));
  zxlogf(INFO, "DW_I2C_RXFLR = \t0x%x\n", I2C_DW_READ32(DW_I2C_RXFLR));
  zxlogf(INFO, "DW_I2C_COMP_PARAM_1 = \t0x%x\n", I2C_DW_READ32(DW_I2C_COMP_PARAM_1));
  zxlogf(INFO, "DW_I2C_TX_ABRT_SOURCE = \t0x%x\n", I2C_DW_READ32(DW_I2C_TX_ABRT_SOURCE));
  return ZX_OK;
}

static zx_status_t i2c_dw_enable_wait(i2c_dw_dev_t* dev, bool enable) {
  int max_poll = 100;
  int poll = 0;

  // set enable bit to 0
  I2C_DW_SET_BITS32(DW_I2C_ENABLE, DW_I2C_ENABLE_ENABLE_START, DW_I2C_ENABLE_ENABLE_BITS, enable);

  do {
    if (I2C_DW_GET_BITS32(DW_I2C_ENABLE_STATUS, DW_I2C_ENABLE_STATUS_EN_START,
                          DW_I2C_ENABLE_STATUS_EN_BITS) == enable) {
      // we are done. exit
      return ZX_OK;
    }
    // sleep 10 times the signaling period for the highest i2c transfer speed (400K) ~25uS
    usleep(25);
  } while (poll++ < max_poll);

  zxlogf(ERROR, "%s: Could not %s I2C contoller! DW_I2C_ENABLE_STATUS = 0x%x\n", __FUNCTION__,
         enable ? "enable" : "disable", I2C_DW_READ32(DW_I2C_ENABLE_STATUS));
  i2c_dw_dumpstate(dev);

  return ZX_ERR_TIMED_OUT;
}

static zx_status_t i2c_dw_enable(i2c_dw_dev_t* dev) { return i2c_dw_enable_wait(dev, I2C_ENABLE); }

static void i2c_dw_clear_interrupts(i2c_dw_dev_t* dev) {
  I2C_DW_READ32(DW_I2C_CLR_INTR);  // reading this register will clear all the interrupts
}

static void i2c_dw_disable_interrupts(i2c_dw_dev_t* dev) { I2C_DW_WRITE32(DW_I2C_INTR_MASK, 0); }

static void i2c_dw_enable_interrupts(i2c_dw_dev_t* dev, uint32_t flag) {
  I2C_DW_WRITE32(DW_I2C_INTR_MASK, flag);
}

static zx_status_t i2c_dw_disable(i2c_dw_dev_t* dev) {
  return i2c_dw_enable_wait(dev, I2C_DISABLE);
}

static zx_status_t i2c_dw_wait_event(i2c_dw_dev_t* dev, uint32_t sig_mask) {
  uint32_t observed = 0;
  zx_time_t deadline = zx_deadline_after(dev->timeout);

  sig_mask |= I2C_ERROR_SIGNAL;
  zx_status_t status = zx_object_wait_one(dev->event_handle, sig_mask, deadline, &observed);
  if (status != ZX_OK) {
    return status;
  }

  status = zx_object_signal(dev->event_handle, observed, 0);
  if (status != ZX_OK) {
    return status;
  }

  if (observed & I2C_ERROR_SIGNAL) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

// Function to read and clear interrupts once read
static uint32_t i2c_dw_read_clear_irq(i2c_dw_dev_t* dev) {
  uint32_t irq = I2C_DW_READ32(DW_I2C_INTR_STAT);

  if (irq & DW_I2C_INTR_TX_ABRT) {
    // ABRT_SOURCE should be read before clearing TX_ABRT
    zxlogf(ERROR, "dw-i2c: error on bus - Abort source 0x%x\n",
           I2C_DW_READ32(DW_I2C_TX_ABRT_SOURCE));
    I2C_DW_READ32(DW_I2C_CLR_TX_ABRT);
  }
  if (irq & DW_I2C_INTR_START_DET) {
    I2C_DW_READ32(DW_I2C_INTR_START_DET);
  }
  if (irq & DW_I2C_INTR_ACTIVITY) {
    I2C_DW_READ32(DW_I2C_CLR_ACTIVITY);
  }
  if (irq & DW_I2C_INTR_STOP_DET) {
    I2C_DW_READ32(DW_I2C_CLR_STOP_DET);
  }
  return irq;
}

static zx_status_t i2c_dw_receive(i2c_dw_dev_t* dev) {
  if (dev->rx_pending == 0) {
    zxlogf(ERROR, "dw-i2c: Bytes received without being requested\n");
    return ZX_ERR_IO_OVERRUN;
  }

  uint32_t avail_read = I2C_DW_READ32(DW_I2C_RXFLR);

  while ((avail_read != 0) && (dev->rx_op_idx < dev->ops_count)) {
    i2c_impl_op_t* op = &dev->ops[dev->rx_op_idx];
    if (!op->is_read) {
      dev->rx_op_idx++;
      continue;
    }
    uint8_t* buff = op->data_buffer + dev->rx_done_len;
    *buff = I2C_DW_GET_BITS32(DW_I2C_DATA_CMD, DW_I2C_DATA_CMD_DAT_START, DW_I2C_DATA_CMD_DAT_BITS);
    dev->rx_done_len++;
    dev->rx_pending--;
    if (dev->rx_done_len == op->data_size) {
      dev->rx_op_idx++;
      dev->rx_done_len = 0;
    }
    avail_read--;
  }

  if (avail_read != 0) {
    zxlogf(ERROR, "dw-i2c: %d more bytes received than requested\n", avail_read);
    return ZX_ERR_IO_OVERRUN;
  }

  return ZX_OK;
}

static zx_status_t i2c_dw_transmit(i2c_dw_dev_t* dev) {
  uint32_t tx_limit;

  tx_limit = dev->tx_fifo_depth - I2C_DW_READ32(DW_I2C_TXFLR);

  // TODO(ZX-4628)
  // if IC_EMPTYFIFO_HOLD_MASTER_EN = 0, then STOP is sent on TX_EMPTY. All commands should be
  // queued up as soon as possible to avoid this. Possible race leading to failed
  // transaction, if the irq thread is deschedule in the midst for tx command queuing.
  // This is the mode used in as370 and currently this issue is not addressed.
  // See bug ZX-4628 for details.
  // if IC_EMPTYFIFO_HOLD_MASTER_EN = 1, then STOP and RESTART must be sent explicitly, which is
  // handled by this code.
  while ((tx_limit != 0) && (dev->tx_op_idx < dev->ops_count)) {
    i2c_impl_op_t* op = &dev->ops[dev->tx_op_idx];
    uint8_t* buff = op->data_buffer + dev->tx_done_len;
    size_t len = op->data_size - dev->tx_done_len;
    ZX_DEBUG_ASSERT(len <= I2C_DW_MAX_TRANSFER);

    uint32_t cmd = 0;
    // send STOP cmd if last byte and stop set
    if (len == 1 && op->stop) {
      cmd = I2C_DW_SET_MASK(cmd, DW_I2C_DATA_CMD_STOP_START, DW_I2C_DATA_CMD_STOP_BITS, 1);
    }

    // send restart
    if (dev->send_restart) {
      cmd = I2C_DW_SET_MASK(cmd, DW_I2C_DATA_CMD_RESTART_START, DW_I2C_DATA_CMD_RESTART_BITS, 1);
      dev->send_restart = false;
    }

    if (op->is_read) {
      // Read command should be queued for each byte.
      I2C_DW_WRITE32(DW_I2C_DATA_CMD, cmd | (1 << DW_I2C_DATA_CMD_CMD_START));
      dev->rx_pending++;
      // Set receive threshold to 1 less than the expected size.
      // Do this only once.
      if (dev->tx_done_len == 0) {
        I2C_DW_SET_BITS32(DW_I2C_RX_TL, DW_I2C_RX_TL_START, DW_I2C_RX_TL_BITS, op->data_size - 1);
      }
    } else {
      I2C_DW_WRITE32(DW_I2C_DATA_CMD, cmd | *buff++);
    }
    dev->tx_done_len++;

    if (dev->tx_done_len == op->data_size) {
      dev->tx_op_idx++;
      dev->tx_done_len = 0;
      dev->send_restart = true;
    }
    tx_limit--;
  }

  if (dev->tx_op_idx == dev->ops_count) {
    // All tx are complete. Remove TX_EMPTY from interrupt mask.
    i2c_dw_enable_interrupts(dev, DW_I2C_INTR_READ_INTR_MASK);
  }

  return ZX_OK;
}

// Thread to handle interrupts
static int i2c_dw_irq_thread(void* arg) {
  i2c_dw_dev_t* dev = (i2c_dw_dev_t*)arg;
  zx_status_t status;
  uint32_t irq;

  while (1) {
    status = zx_interrupt_wait(dev->irq_handle, NULL);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: irq wait failed, retcode = %d\n", __FUNCTION__, status);
      break;
    }

    mtx_lock(&dev->ops_lock);

    if (dev->ops == NULL) {
      mtx_unlock(&dev->ops_lock);
      continue;
    }

    irq = i2c_dw_read_clear_irq(dev);

    if (irq & DW_I2C_INTR_TX_ABRT) {
      status = zx_object_signal(dev->event_handle, 0, I2C_ERROR_SIGNAL);
      if (status != ZX_OK) {
        zxlogf(ERROR, "Failure signaling I2C error - %d\n", status);
      }
      dev->ops = NULL;
    }

    if (irq & DW_I2C_INTR_RX_FULL) {
      if (i2c_dw_receive(dev) != ZX_OK) {
        dev->ops = NULL;
        status = zx_object_signal(dev->event_handle, 0, I2C_ERROR_SIGNAL);
        if (status != ZX_OK) {
          zxlogf(ERROR, "Failure signaling I2C error - %d\n", status);
        }
      }
    }

    if (irq & DW_I2C_INTR_TX_EMPTY) {
      if (i2c_dw_transmit(dev) != ZX_OK) {
        dev->ops = NULL;
        status = zx_object_signal(dev->event_handle, 0, I2C_ERROR_SIGNAL);
        if (status != ZX_OK) {
          zxlogf(ERROR, "Failure signaling I2C error - %d\n", status);
        }
      }
    }

    if (irq & DW_I2C_INTR_STOP_DET) {
      // signal when all tx/rx are complete
      if ((dev->tx_op_idx == dev->ops_count) && (dev->rx_pending == 0)) {
        dev->ops = NULL;
        status = zx_object_signal(dev->event_handle, 0, I2C_TXN_COMPLETE_SIGNAL);
        if (status != ZX_OK) {
          zxlogf(ERROR, "Failure signaling I2C complete - %d\n", status);
        }
      }
    }
    mtx_unlock(&dev->ops_lock);
  }

  return ZX_OK;
}

static zx_status_t i2c_dw_wait_bus_busy(i2c_dw_dev_t* dev) {
  uint32_t timeout = 0;
  uint32_t mask = I2C_DW_SET_MASK(0, DW_I2C_STATUS_ACTIVITY_START, DW_I2C_STATUS_ACTIVITY_BITS, 1);
  while (I2C_DW_READ32(DW_I2C_STATUS) & mask) {
    if (timeout > 100) {
      return ZX_ERR_TIMED_OUT;
    }
    usleep(10);
    timeout++;
  }
  return ZX_OK;
}

static void i2c_dw_set_ops(i2c_dw_dev_t* dev, i2c_impl_op_t* ops, size_t count) {
  mtx_lock(&dev->ops_lock);
  dev->ops = ops;
  dev->ops_count = count;
  dev->rx_op_idx = 0;
  dev->tx_op_idx = 0;
  dev->rx_done_len = 0;
  dev->tx_done_len = 0;
  dev->send_restart = false;
  dev->rx_pending = 0;
  mtx_unlock(&dev->ops_lock);
}

static zx_status_t i2c_dw_transact(void* ctx, uint32_t bus_id, const i2c_impl_op_t* rws,
                                   size_t count) {
  size_t i;
  for (i = 0; i < count; ++i) {
    if (rws[i].data_size > I2C_DW_MAX_TRANSFER) {
      return ZX_ERR_OUT_OF_RANGE;
    }
  }

  i2c_dw_t* i2c = ctx;

  if (bus_id >= i2c->i2c_dev_count) {
    return ZX_ERR_INVALID_ARGS;
  }

  i2c_dw_dev_t* dev = &i2c->i2c_devs[bus_id];

  if (count == 0) {
    return ZX_OK;
  }
  for (i = 1; i < count; ++i) {
    if (rws[i].address != rws[0].address) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  mtx_lock(&dev->transact_lock);

  zx_status_t status = i2c_dw_wait_bus_busy(dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2C bus wait failed %d\n", status);
    mtx_unlock(&dev->transact_lock);
    return status;
  }
  status = i2c_dw_set_slave_addr(dev, rws[0].address);
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2C set address failed %d\n", status);
    mtx_unlock(&dev->transact_lock);
    return status;
  }

  i2c_dw_disable_interrupts(dev);

  i2c_dw_set_ops(dev, (i2c_impl_op_t*)rws, count);

  status = i2c_dw_enable(dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2C device enable failed %d\n", status);
    mtx_unlock(&dev->transact_lock);
    return status;
  }

  zx_object_signal(dev->event_handle, I2C_ALL_SIGNALS, 0);
  i2c_dw_clear_interrupts(dev);
  i2c_dw_enable_interrupts(dev, DW_I2C_INTR_DEFAULT_INTR_MASK);

  status = i2c_dw_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);

  i2c_dw_set_ops(dev, NULL, 0);

  zx_status_t disable_ret = i2c_dw_disable(dev);
  if (disable_ret != ZX_OK) {
    zxlogf(ERROR, "I2C device disable failed %d\n", disable_ret);
  }

  mtx_unlock(&dev->transact_lock);
  return status;
}

static zx_status_t i2c_dw_set_bitrate(void* ctx, uint32_t bus_id, uint32_t bitrate) {
  // TODO: currently supports FAST_MODE - 400kHz
  return ZX_ERR_NOT_SUPPORTED;
}

static uint32_t i2c_dw_get_bus_count(void* ctx) {
  i2c_dw_t* i2c = ctx;

  return i2c->i2c_dev_count;
}

static zx_status_t i2c_dw_get_max_transfer_size(void* ctx, uint32_t bus_id, size_t* out_size) {
  *out_size = I2C_DW_MAX_TRANSFER;
  return ZX_OK;
}

static zx_status_t i2c_dw_set_slave_addr(i2c_dw_dev_t* dev, uint16_t addr) {
  addr &= 0x7f;  // support 7bit for now
  uint32_t reg = I2C_DW_READ32(DW_I2C_TAR);
  reg = I2C_DW_SET_MASK(reg, DW_I2C_TAR_TAR_START, DW_I2C_TAR_TAR_BITS, addr);
  reg = I2C_DW_SET_MASK(reg, DW_I2C_TAR_10BIT_START, DW_I2C_TAR_10BIT_BITS, 0);
  I2C_DW_WRITE32(DW_I2C_TAR, reg);
  return ZX_OK;
}

static zx_status_t i2c_dw_host_init(i2c_dw_dev_t* dev) {
  uint32_t dw_comp_type;
  uint32_t regval;

  // Make sure we are truly running on a DesignWire IP
  dw_comp_type = I2C_DW_READ32(DW_I2C_COMP_TYPE);

  if (dw_comp_type != I2C_DW_COMP_TYPE_NUM) {
    zxlogf(ERROR, "%s: Incompatible IP Block detected. Expected = 0x%x, Actual = 0x%x\n",
           __FUNCTION__, I2C_DW_COMP_TYPE_NUM, dw_comp_type);

    return ZX_ERR_NOT_SUPPORTED;
  }

  // read the various capabilities of the component
  dev->tx_fifo_depth = I2C_DW_GET_BITS32(DW_I2C_COMP_PARAM_1, DW_I2C_COMP_PARAM_1_TXFIFOSZ_START,
                                         DW_I2C_COMP_PARAM_1_TXFIFOSZ_BITS);
  dev->rx_fifo_depth = I2C_DW_GET_BITS32(DW_I2C_COMP_PARAM_1, DW_I2C_COMP_PARAM_1_RXFIFOSZ_START,
                                         DW_I2C_COMP_PARAM_1_RXFIFOSZ_BITS);

  /* I2C Block Initialization based on DW_apb_i2c_databook Section 7.3 */

  // disable I2C Block
  i2c_dw_disable(dev);

  // configure the controller:
  // - slave disable
  regval = 0;
  regval =
      I2C_DW_SET_MASK(regval, DW_I2C_CON_SLAVE_DIS_START, DW_I2C_CON_SLAVE_DIS_BITS, I2C_ENABLE);

  // - enable restart mode
  regval =
      I2C_DW_SET_MASK(regval, DW_I2C_CON_RESTART_EN_START, DW_I2C_CON_RESTART_EN_BITS, I2C_ENABLE);

  // - set 7-bit address modeset
  regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_10BITADDRSLAVE_START, DW_I2C_CON_10BITADDRSLAVE_BITS,
                           I2C_7BIT_ADDR);
  regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_10BITADDRMASTER_START,
                           DW_I2C_CON_10BITADDRMASTER_BITS, I2C_7BIT_ADDR);

  // - set speed to fast, master enable
  regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_SPEED_START, DW_I2C_CON_SPEED_BITS, I2C_FAST_MODE);

  // - set master enable
  regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_MASTER_MODE_START, DW_I2C_CON_MASTER_MODE_BITS,
                           I2C_ENABLE);

  // write final mask
  I2C_DW_WRITE32(DW_I2C_CON, regval);

  // write SS/FS LCNT and HCNT
  I2C_DW_SET_BITS32(DW_I2C_SS_SCL_HCNT, DW_I2C_SS_SCL_HCNT_START, DW_I2C_SS_SCL_HCNT_BITS,
                    DW_I2C_SS_SCL_HCNT_VALUE);
  I2C_DW_SET_BITS32(DW_I2C_SS_SCL_LCNT, DW_I2C_SS_SCL_LCNT_START, DW_I2C_SS_SCL_LCNT_BITS,
                    DW_I2C_SS_SCL_LCNT_VALUE);
  I2C_DW_SET_BITS32(DW_I2C_FS_SCL_HCNT, DW_I2C_FS_SCL_HCNT_START, DW_I2C_FS_SCL_HCNT_BITS,
                    DW_I2C_FS_SCL_HCNT_VALUE);
  I2C_DW_SET_BITS32(DW_I2C_FS_SCL_LCNT, DW_I2C_FS_SCL_LCNT_START, DW_I2C_FS_SCL_LCNT_BITS,
                    DW_I2C_FS_SCL_LCNT_VALUE);

  // set SDA Hold time
  // enable SDA hold for RX as well
  regval = DW_I2C_SDA_HOLD_VALUE | DW_I2C_SDA_HOLD_RX_MASK;
  I2C_DW_SET_BITS32(DW_I2C_SDA_HOLD, DW_I2C_SDA_HOLD_START, DW_I2C_SDA_HOLD_BITS, regval);

  // setup TX FIFO Thresholds
  I2C_DW_SET_BITS32(DW_I2C_TX_TL, DW_I2C_TX_TL_START, DW_I2C_TX_TL_BITS, dev->tx_fifo_depth / 2);
  I2C_DW_SET_BITS32(DW_I2C_RX_TL, DW_I2C_RX_TL_START, DW_I2C_RX_TL_BITS, 0);

  // disable interrupts
  i2c_dw_disable_interrupts(dev);

  return ZX_OK;
}

#if I2C_AS370_DW_TEST
static int i2c_dw_test_thread(void* ctx) {
  zx_status_t status;
  uint8_t addr = 0;
  bool pass = true;
  uint8_t valid_addr = 0x66;   // SY20212DAIC PMIC device
  uint8_t valid_value = 0x8B;  // Register 0x0 default value for PMIC
  uint8_t data_write = 0;
  uint8_t data_read;
#if 0
  zxlogf(INFO, "Finding I2c devices\n");
  // Find all available devices
  for (uint32_t i = 0x0; i <= 0x7f; i++) {
    addr = i;
    i2c_impl_op_t ops[] = {
        {.address = addr,
         .data_buffer = &data_write,
         .data_size = 1,
         .is_read = false,
         .stop = false},
        {.address = addr, .data_buffer = &data_read, .data_size = 1, .is_read = true, .stop = true},
    };

    status = i2c_dw_transact(ctx, 0, ops, countof(ops));
    if (status == ZX_OK) {
      zxlogf(INFO, "I2C device found at address: 0x%02X\n", addr);
    }
  }
#endif
  zxlogf(INFO, "I2C: Testing PMIC ping\n");

  // Test multiple reads from a known device
  for (uint32_t i = 0; i < 10; i++) {
    addr = valid_addr;
    data_read = 0;
    i2c_impl_op_t ops[] = {
        {.address = addr,
         .data_buffer = &data_write,
         .data_size = 1,
         .is_read = false,
         .stop = false},
        {.address = addr, .data_buffer = &data_read, .data_size = 1, .is_read = true, .stop = true},
    };

    status = i2c_dw_transact(ctx, 0, ops, countof(ops));
    if (status == ZX_OK) {
      // Check with reset value of PMIC registers
      if (data_read != valid_value) {
        zxlogf(INFO, "I2C test: PMIC register value does not matched - %x\n", data_read);
        pass = false;
      }
    } else {
      zxlogf(INFO, "I2C test: PMIC ping failed : %d\n", status);
      pass = false;
    }
  }

  if (pass) {
    zxlogf(INFO, "DW I2C test for AS370 passed\n");
  } else {
    zxlogf(ERROR, "DW I2C test for AS370 failed\n");
  }
  return 0;
}
#endif

static zx_status_t i2c_dw_init(i2c_dw_t* i2c, uint32_t index) {
  zx_status_t status;
  i2c_dw_dev_t* device = &i2c->i2c_devs[index];

  device->timeout = ZX_MSEC(100);

  status = pdev_map_mmio_buffer(&i2c->pdev, index, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                &device->regs_iobuff);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_map_mmio_buffer failed %d\n", __FUNCTION__, status);
    goto init_fail;
  }

  status = pdev_get_interrupt(&i2c->pdev, index, 0, &device->irq_handle);
  if (status != ZX_OK) {
    goto init_fail;
  }

  status = zx_event_create(0, &device->event_handle);
  if (status != ZX_OK) {
    goto init_fail;
  }

  mtx_init(&device->transact_lock, mtx_plain);
  mtx_init(&device->ops_lock, mtx_plain);

  // initialize i2c host controller
  status = i2c_dw_host_init(device);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to initialize i2c host controller %d", __FUNCTION__, status);
    goto init_fail;
  }

  thrd_t irq_thread;
  thrd_create_with_name(&irq_thread, i2c_dw_irq_thread, device, "i2c_dw_irq_thread");

#if I2C_AS370_DW_TEST
  if (index == 0) {
    thrd_t test_thread;
    thrd_create_with_name(&test_thread, i2c_dw_test_thread, i2c, "i2c_dw_test_thread");
  }
#endif
  return ZX_OK;

init_fail:
  if (device) {
    mmio_buffer_release(&device->regs_iobuff);
    if (device->event_handle != ZX_HANDLE_INVALID) {
      zx_handle_close(device->event_handle);
    }
    if (device->irq_handle != ZX_HANDLE_INVALID) {
      zx_handle_close(device->irq_handle);
    }
    free(device);
  }
  return status;
}

static i2c_impl_protocol_ops_t i2c_ops = {
    .get_bus_count = i2c_dw_get_bus_count,
    .get_max_transfer_size = i2c_dw_get_max_transfer_size,
    .set_bitrate = i2c_dw_set_bitrate,
    .transact = i2c_dw_transact,
};

static zx_protocol_device_t i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
};

static zx_status_t dw_i2c_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  i2c_dw_t* i2c = calloc(1, sizeof(i2c_dw_t));
  if (!i2c) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &i2c->pdev)) != ZX_OK) {
    zxlogf(ERROR, "dw_i2c_bind: ZX_PROTOCOL_PDEV not available\n");
    goto fail;
  }

  pdev_device_info_t info;
  status = pdev_get_device_info(&i2c->pdev, &info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dw_i2c_bind: pdev_get_device_info failed\n");
    goto fail;
  }

  if (info.mmio_count != info.irq_count) {
    zxlogf(ERROR, "dw_i2c_bind: mmio_count %u does not matchirq_count %u\n", info.mmio_count,
           info.irq_count);
    status = ZX_ERR_INVALID_ARGS;
    goto fail;
  }

  i2c->i2c_devs = calloc(info.mmio_count, sizeof(i2c_dw_dev_t));
  if (!i2c->i2c_devs) {
    free(i2c);
    return ZX_ERR_NO_MEMORY;
  }
  i2c->i2c_dev_count = info.mmio_count;

  for (uint32_t i = 0; i < i2c->i2c_dev_count; i++) {
    zx_status_t status = i2c_dw_init(i2c, i);
    if (status != ZX_OK) {
      zxlogf(ERROR, "dw_i2c_bind: dw_i2c_dev_init failed: %d\n", status);
      goto fail;
    }
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "dw-i2c",
      .ctx = i2c,
      .ops = &i2c_device_proto,
      .proto_id = ZX_PROTOCOL_I2C_IMPL,
      .proto_ops = &i2c_ops,
  };

  status = device_add(parent, &args, &i2c->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dw_i2c_bind: device_add failed\n");
    goto fail;
  }

  return ZX_OK;

fail:
  return status;
}

static zx_driver_ops_t dw_i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dw_i2c_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(dw_i2c, dw_i2c_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_DW_I2C),
ZIRCON_DRIVER_END(dw_i2c)
//clang-format on
