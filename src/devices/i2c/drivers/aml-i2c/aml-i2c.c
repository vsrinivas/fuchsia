// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/i2cimpl/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ddk/trace/event.h>
#include <lib/device-protocol/platform-device.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <bits/limits.h>

#include "src/devices/i2c/drivers/aml-i2c/aml_i2c_bind.h"

#define I2C_ERROR_SIGNAL ZX_USER_SIGNAL_0
#define I2C_TXN_COMPLETE_SIGNAL ZX_USER_SIGNAL_1

#define AML_I2C_CONTROL_REG_START (uint32_t)(1 << 0)
#define AML_I2C_CONTROL_REG_ACK_IGNORE (uint32_t)(1 << 1)
#define AML_I2C_CONTROL_REG_STATUS (uint32_t)(1 << 2)
#define AML_I2C_CONTROL_REG_ERR (uint32_t)(1 << 3)

#define AML_I2C_CONTROL_REG_QTR_CLK_DLY_MAX 0x3ff
#define AML_I2C_CONTROL_REG_QTR_CLK_DLY_SHIFT 12
#define AML_I2C_CONTROL_REG_QTR_CLK_DLY_MASK \
  (uint32_t)(AML_I2C_CONTROL_REG_QTR_CLK_DLY_MAX << AML_I2C_CONTROL_REG_QTR_CLK_DLY_SHIFT)

#define AML_I2C_MAX_TRANSFER 512

typedef volatile struct {
  uint32_t control;
  uint32_t slave_addr;
  uint32_t token_list_0;
  uint32_t token_list_1;
  uint32_t token_wdata_0;
  uint32_t token_wdata_1;
  uint32_t token_rdata_0;
  uint32_t token_rdata_1;
} __PACKED aml_i2c_regs_t;

typedef enum {
  TOKEN_END,
  TOKEN_START,
  TOKEN_SLAVE_ADDR_WR,
  TOKEN_SLAVE_ADDR_RD,
  TOKEN_DATA,
  TOKEN_DATA_LAST,
  TOKEN_STOP
} aml_i2c_token_t;

typedef struct {
  zx_handle_t irq;
  zx_handle_t event;
  mmio_buffer_t regs_iobuff;
  MMIO_PTR aml_i2c_regs_t* virt_regs;
  zx_duration_t timeout;
} aml_i2c_dev_t;

typedef struct {
  pdev_protocol_t pdev;
  zx_device_t* zxdev;
  aml_i2c_dev_t* i2c_devs;
  uint32_t dev_count;
} aml_i2c_t;

static zx_status_t aml_i2c_set_slave_addr(aml_i2c_dev_t* dev, uint16_t addr) {
  addr &= 0x7f;
  uint32_t reg = MmioRead32(&dev->virt_regs->slave_addr);
  reg = reg & ~0xff;
  reg = reg | ((addr << 1) & 0xff);
  MmioWrite32(reg, &dev->virt_regs->slave_addr);

  return ZX_OK;
}

static int aml_i2c_irq_thread(void* arg) {
  aml_i2c_dev_t* dev = arg;
  zx_status_t status;

  while (1) {
    status = zx_interrupt_wait(dev->irq, NULL);
    if (status != ZX_OK) {
      zxlogf(ERROR, "i2c: interrupt error");
      continue;
    }
    uint32_t reg = MmioRead32(&dev->virt_regs->control);
    if (reg & AML_I2C_CONTROL_REG_ERR) {
      zx_object_signal(dev->event, 0, I2C_ERROR_SIGNAL);
      zxlogf(ERROR, "i2c: error on bus");
    } else {
      zx_object_signal(dev->event, 0, I2C_TXN_COMPLETE_SIGNAL);
    }
  }
  return ZX_OK;
}

#if 0
static zx_status_t aml_i2c_dumpstate(aml_i2c_dev_t* dev) {
  printf("control reg      : %08x\n", MmioRead32(&dev->virt_regs->control));
  printf("slave addr  reg  : %08x\n", MmioRead32(&dev->virt_regs->slave_addr));
  printf("token list0 reg  : %08x\n", MmioRead32(&dev->virt_regs->token_list_0));
  printf("token list1 reg  : %08x\n", MmioRead32(&dev->virt_regs->token_list_1));
  printf("token wdata0     : %08x\n", MmioRead32(&dev->virt_regs->token_wdata_0));
  printf("token wdata1     : %08x\n", MmioRead32(&dev->virt_regs->token_wdata_1));
  printf("token rdata0     : %08x\n", MmioRead32(&dev->virt_regs->token_rdata_0));
  printf("token rdata1     : %08x\n", MmioRead32(&dev->virt_regs->token_rdata_1));

  return ZX_OK;
}
#endif

static inline void MmioClearBits32(uint32_t bits, MMIO_PTR volatile uint32_t* buffer) {
  uint32_t reg = MmioRead32(buffer);
  reg &= ~bits;
  MmioWrite32(reg, buffer);
}

static inline void MmioSetBits32(uint32_t bits, MMIO_PTR volatile uint32_t* buffer) {
  uint32_t reg = MmioRead32(buffer);
  reg |= bits;
  MmioWrite32(reg, buffer);
}

static zx_status_t aml_i2c_start_xfer(aml_i2c_dev_t* dev) {
  // First have to clear the start bit before setting (RTFM)
  MmioClearBits32(AML_I2C_CONTROL_REG_START, &dev->virt_regs->control);
  MmioSetBits32(AML_I2C_CONTROL_REG_START, &dev->virt_regs->control);
  return ZX_OK;
}

static zx_status_t aml_i2c_wait_event(aml_i2c_dev_t* dev, uint32_t sig_mask) {
  zx_time_t deadline = zx_deadline_after(dev->timeout);
  uint32_t observed;
  sig_mask |= I2C_ERROR_SIGNAL;
  zx_status_t status = zx_object_wait_one(dev->event, sig_mask, deadline, &observed);
  if (status != ZX_OK) {
    return status;
  }
  zx_object_signal(dev->event, observed, 0);
  if (observed & I2C_ERROR_SIGNAL)
    return ZX_ERR_TIMED_OUT;
  return ZX_OK;
}

static zx_status_t aml_i2c_write(aml_i2c_dev_t* dev, const uint8_t* buff, uint32_t len, bool stop) {
  TRACE_DURATION("i2c", "aml-i2c Write");
  ZX_DEBUG_ASSERT(len <= AML_I2C_MAX_TRANSFER);
  uint32_t token_num = 0;
  uint64_t token_reg = 0;

  token_reg |= (uint64_t)TOKEN_START << (4 * (token_num++));
  token_reg |= (uint64_t)TOKEN_SLAVE_ADDR_WR << (4 * (token_num++));

  while (len > 0) {
    bool is_last_iter = len <= 8;
    uint32_t tx_size = is_last_iter ? len : 8;
    for (uint32_t i = 0; i < tx_size; i++) {
      token_reg |= (uint64_t)TOKEN_DATA << (4 * (token_num++));
    }

    if (is_last_iter && stop) {
      token_reg |= (uint64_t)TOKEN_STOP << (4 * (token_num++));
    }

    MmioWrite32(token_reg & 0xffffffff, &dev->virt_regs->token_list_0);
    token_reg = token_reg >> 32;
    MmioWrite32(token_reg & 0xffffffff, &dev->virt_regs->token_list_1);

    uint64_t wdata = 0;
    for (uint32_t i = 0; i < tx_size; i++) {
      wdata |= (uint64_t)buff[i] << (8 * i);
    }

    MmioWrite32(wdata & 0xffffffff, &dev->virt_regs->token_wdata_0);
    MmioWrite32((wdata >> 32) & 0xffffffff, &dev->virt_regs->token_wdata_1);

    aml_i2c_start_xfer(dev);
    // while (dev->virt_regs->control & 0x4) ;;    // wait for idle
    zx_status_t status = aml_i2c_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);
    if (status != ZX_OK) {
      return status;
    }

    len -= tx_size;
    buff += tx_size;
    token_num = 0;
    token_reg = 0;
  }

  return ZX_OK;
}

static zx_status_t aml_i2c_read(aml_i2c_dev_t* dev, uint8_t* buff, uint32_t len, bool stop) {
  ZX_DEBUG_ASSERT(len <= AML_I2C_MAX_TRANSFER);
  TRACE_DURATION("i2c", "aml-i2c Read");
  uint32_t token_num = 0;
  uint64_t token_reg = 0;

  token_reg |= (uint64_t)TOKEN_START << (4 * (token_num++));
  token_reg |= (uint64_t)TOKEN_SLAVE_ADDR_RD << (4 * (token_num++));

  while (len > 0) {
    bool is_last_iter = len <= 8;
    uint32_t rx_size = is_last_iter ? len : 8;

    for (uint32_t i = 0; i < (rx_size - 1); i++) {
      token_reg |= (uint64_t)TOKEN_DATA << (4 * (token_num++));
    }
    if (is_last_iter) {
      token_reg |= (uint64_t)TOKEN_DATA_LAST << (4 * (token_num++));
      if (stop) {
        token_reg |= (uint64_t)TOKEN_STOP << (4 * (token_num++));
      }
    } else {
      token_reg |= (uint64_t)TOKEN_DATA << (4 * (token_num++));
    }

    MmioWrite32(token_reg & 0xffffffff, &dev->virt_regs->token_list_0);
    token_reg = token_reg >> 32;
    MmioWrite32(token_reg & 0xffffffff, &dev->virt_regs->token_list_1);

    // clear registers to prevent data leaking from last xfer
    MmioWrite32(0, &dev->virt_regs->token_rdata_0);
    MmioWrite32(0, &dev->virt_regs->token_rdata_1);

    aml_i2c_start_xfer(dev);

    zx_status_t status = aml_i2c_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);
    if (status != ZX_OK) {
      return status;
    }

    // while (dev->virt_regs->control & 0x4) ;;    // wait for idle

    uint64_t rdata;
    rdata = MmioRead32(&dev->virt_regs->token_rdata_0);
    rdata |= ((uint64_t)MmioRead32(&dev->virt_regs->token_rdata_1)) << 32;

    for (uint32_t i = 0; i < sizeof(rdata); i++) {
      buff[i] = (uint8_t)((rdata >> (8 * i) & 0xff));
    }

    len -= rx_size;
    buff += rx_size;
    token_num = 0;
    token_reg = 0;
  }

  return ZX_OK;
}

/* create instance of aml_i2c_t and do basic initialization.  There will
be one of these instances for each of the soc i2c ports.
*/
static zx_status_t aml_i2c_dev_init(aml_i2c_t* i2c, unsigned index, uint32_t clock_delay) {
  aml_i2c_dev_t* device = &i2c->i2c_devs[index];

  device->timeout = ZX_SEC(1);

  zx_status_t status;

  status = pdev_map_mmio_buffer(&i2c->pdev, index, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                &device->regs_iobuff);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_i2c_dev_init: pdev_map_mmio_buffer failed %d", status);
    return status;
  }

  device->virt_regs = (MMIO_PTR aml_i2c_regs_t*)device->regs_iobuff.vaddr;

  if (clock_delay > AML_I2C_CONTROL_REG_QTR_CLK_DLY_MAX) {
    zxlogf(ERROR, "aml_i2c_dev_init: invalid clock delay");
    return ZX_ERR_INVALID_ARGS;
  }

  if (clock_delay > 0) {
    uint32_t control = MmioRead32(&device->virt_regs->control);
    control &= ~AML_I2C_CONTROL_REG_QTR_CLK_DLY_MASK;
    control |= clock_delay << AML_I2C_CONTROL_REG_QTR_CLK_DLY_SHIFT;
    MmioWrite32(control, &device->virt_regs->control);
  }

  status = pdev_get_interrupt(&i2c->pdev, index, 0, &device->irq);
  if (status != ZX_OK) {
    return status;
  }

  status = zx_event_create(0, &device->event);
  if (status != ZX_OK) {
    return status;
  }

  thrd_t irqthrd;
  thrd_create_with_name(&irqthrd, aml_i2c_irq_thread, device, "i2c_irq_thread");

  // Set profile for IRQ thread.
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  const zx_duration_t capacity = ZX_USEC(20);
  const zx_duration_t deadline = ZX_USEC(100);
  const zx_duration_t period = deadline;

  zx_handle_t irq_profile = ZX_HANDLE_INVALID;
  status = device_get_deadline_profile(i2c->zxdev, capacity, deadline, period, "aml_i2c_irq_thread",
                                       &irq_profile);
  if (status != ZX_OK) {
    zxlogf(WARNING, "aml_i2c_dev_init: Failed to get deadline profile: %s",
           zx_status_get_string(status));
  } else {
    status = zx_object_set_profile(thrd_get_zx_handle(irqthrd), irq_profile, 0);
    if (status != ZX_OK) {
      zxlogf(WARNING, "aml_i2c_dev_init: Failed to apply deadline profile to IRQ thread: %s",
             zx_status_get_string(status));
    }
    zx_handle_close(irq_profile);
  }

  return ZX_OK;
}

static uint32_t aml_i2c_get_bus_count(void* ctx) {
  aml_i2c_t* i2c = ctx;

  return i2c->dev_count;
}

static uint32_t aml_i2c_get_bus_base(void* ctx) { return 0; }

static zx_status_t aml_i2c_get_max_transfer_size(void* ctx, uint32_t bus_id, size_t* out_size) {
  *out_size = AML_I2C_MAX_TRANSFER;
  return ZX_OK;
}

static zx_status_t aml_i2c_set_bitrate(void* ctx, uint32_t bus_id, uint32_t bitrate) {
  // TODO(hollande,voydanoff) implement this
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t aml_i2c_transact(void* ctx, uint32_t bus_id, const i2c_impl_op_t* rws,
                                    size_t count) {
  TRACE_DURATION("i2c", "aml-i2c Transact");
  size_t i;
  for (i = 0; i < count; ++i) {
    if (rws[i].data_size > AML_I2C_MAX_TRANSFER) {
      return ZX_ERR_OUT_OF_RANGE;
    }
  }
  aml_i2c_t* i2c = ctx;
  if (bus_id >= i2c->dev_count) {
    return ZX_ERR_INVALID_ARGS;
  }
  aml_i2c_dev_t* dev = &i2c->i2c_devs[bus_id];

  zx_status_t status = ZX_OK;
  for (i = 0; i < count; ++i) {
    status = aml_i2c_set_slave_addr(dev, rws[i].address);
    if (status != ZX_OK) {
      return status;
    }
    if (rws[i].is_read) {
      status = aml_i2c_read(dev, rws[i].data_buffer, (uint32_t)rws[i].data_size, rws[i].stop);
    } else {
      status = aml_i2c_write(dev, rws[i].data_buffer, (uint32_t)rws[i].data_size, rws[i].stop);
    }
    if (status != ZX_OK) {
      return status;  // TODO(andresoportus) release the bus
    }
  }

  return status;
}

static i2c_impl_protocol_ops_t i2c_ops = {
    .get_bus_base = aml_i2c_get_bus_base,
    .get_bus_count = aml_i2c_get_bus_count,
    .get_max_transfer_size = aml_i2c_get_max_transfer_size,
    .set_bitrate = aml_i2c_set_bitrate,
    .transact = aml_i2c_transact,
};

static void aml_i2c_release(void* ctx) {
  aml_i2c_t* i2c = ctx;
  for (unsigned i = 0; i < i2c->dev_count; i++) {
    aml_i2c_dev_t* device = &i2c->i2c_devs[i];
    mmio_buffer_release(&device->regs_iobuff);
    zx_handle_close(device->event);
    zx_handle_close(device->irq);
  }
  free(i2c->i2c_devs);
  free(i2c);
}

static zx_protocol_device_t i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_i2c_release,
};

static zx_status_t aml_i2c_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  uint32_t* clock_delays = NULL;

  aml_i2c_t* i2c = calloc(1, sizeof(aml_i2c_t));
  if (!i2c) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &i2c->pdev)) != ZX_OK) {
    zxlogf(ERROR, "aml_i2c_bind: ZX_PROTOCOL_PDEV not available");
    goto fail;
  }

  pdev_device_info_t info;
  status = pdev_get_device_info(&i2c->pdev, &info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_i2c_bind: pdev_get_device_info failed");
    goto fail;
  }

  if (info.mmio_count != info.irq_count) {
    zxlogf(ERROR, "aml_i2c_bind: mmio_count %u does not matchirq_count %u", info.mmio_count,
           info.irq_count);
    status = ZX_ERR_INVALID_ARGS;
    goto fail;
  }

  size_t metadata_size;
  status = device_get_metadata_size(parent, DEVICE_METADATA_PRIVATE, &metadata_size);
  if (status != ZX_OK) {
    metadata_size = 0;
  } else if (metadata_size != (info.mmio_count * sizeof(uint32_t))) {
    zxlogf(ERROR, "aml_i2c_bind: invalid metadata size");
    status = ZX_ERR_INVALID_ARGS;
    goto fail;
  }

  i2c->i2c_devs = calloc(info.mmio_count, sizeof(aml_i2c_dev_t));
  if (!i2c->i2c_devs) {
    goto fail;
  }
  i2c->dev_count = (uint32_t)info.mmio_count;

  if (metadata_size > 0) {
    clock_delays = calloc(info.mmio_count, sizeof(uint32_t));
    if (!clock_delays) {
      status = ZX_ERR_NO_MEMORY;
      goto fail;
    }

    size_t actual;
    status =
        device_get_metadata(parent, DEVICE_METADATA_PRIVATE, clock_delays, metadata_size, &actual);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml_i2c_bind: device_get_metadata failed");
      goto fail;
    }
    if (actual != metadata_size) {
      zxlogf(ERROR, "aml_i2c_bind: metadata size mismatch");
      status = ZX_ERR_INTERNAL;
      goto fail;
    }
  }

  for (unsigned i = 0; i < i2c->dev_count; i++) {
    zx_status_t status = aml_i2c_dev_init(i2c, i, metadata_size > 0 ? clock_delays[i] : 0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml_i2c_bind: aml_i2c_dev_init failed: %d", status);
      goto fail;
    }
  }

  free(clock_delays);
  clock_delays = NULL;

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "aml-i2c",
      .ctx = i2c,
      .ops = &i2c_device_proto,
      .proto_id = ZX_PROTOCOL_I2C_IMPL,
      .proto_ops = &i2c_ops,
  };

  status = device_add(parent, &args, &i2c->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_i2c_bind: device_add failed");
    goto fail;
  }

  return ZX_OK;

fail:
  free(clock_delays);
  aml_i2c_release(i2c);
  return status;
}

static zx_driver_ops_t aml_i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_i2c_bind,
};

ZIRCON_DRIVER(aml_i2c, aml_i2c_driver_ops, "zircon", "0.1");
