// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/platform-device.h>
#include <stdlib.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <gpio/pl061/pl061.h>
#include <soc/hi3660/hi3660-hw.h>

typedef struct {
  zx_device_t* zxdev;
  zx_device_t* parent;
  list_node_t gpios;
} hi3660_gpio_t;

static pl061_gpios_t* find_gpio(hi3660_gpio_t* gpio, uint32_t index) {
  pl061_gpios_t* gpios;
  // TODO(voydanoff) consider using a fancier data structure here
  list_for_every_entry (&gpio->gpios, gpios, pl061_gpios_t, node) {
    if (index >= gpios->gpio_start && index < gpios->gpio_start + gpios->gpio_count) {
      return gpios;
    }
  }
  zxlogf(ERROR, "find_gpio failed for index %u", index);
  return NULL;
}

static zx_status_t hi3660_gpio_config_in(void* ctx, uint32_t index, uint32_t flags) {
  hi3660_gpio_t* gpio = ctx;
  pl061_gpios_t* gpios = find_gpio(gpio, index);
  if (!gpios) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pl061_proto_ops.config_in(gpios, index, flags);
}

static zx_status_t hi3660_gpio_config_out(void* ctx, uint32_t index, uint8_t initial_value) {
  hi3660_gpio_t* gpio = ctx;
  pl061_gpios_t* gpios = find_gpio(gpio, index);
  if (!gpios) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pl061_proto_ops.config_out(gpios, index, initial_value);
}

static zx_status_t hi3660_gpio_set_alt_function(void* ctx, uint32_t index, uint64_t function) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t hi3660_gpio_read(void* ctx, uint32_t index, uint8_t* out_value) {
  hi3660_gpio_t* gpio = ctx;
  pl061_gpios_t* gpios = find_gpio(gpio, index);
  if (!gpios) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pl061_proto_ops.read(gpios, index, out_value);
}

static zx_status_t hi3660_gpio_write(void* ctx, uint32_t index, uint8_t value) {
  zxlogf(INFO, "hi3660_gpio_write %u - %u", index, value);

  hi3660_gpio_t* gpio = ctx;
  pl061_gpios_t* gpios = find_gpio(gpio, index);
  if (!gpios) {
    zxlogf(INFO, "hi3660_gpio_write ZX_ERR_INVALID_ARGS");
    return ZX_ERR_INVALID_ARGS;
  }
  return pl061_proto_ops.write(gpios, index, value);
}

static zx_status_t hi3660_gpio_get_interrupt(void* ctx, uint32_t pin, uint32_t flags,
                                             zx_handle_t* out_handle) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t hi3660_gpio_release_interrupt(void* ctx, uint32_t pin) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t hi3660_gpio_set_polarity(void* ctx, uint32_t pin, uint32_t polarity) {
  return ZX_ERR_NOT_SUPPORTED;
}

static gpio_impl_protocol_ops_t gpio_ops = {
    .config_in = hi3660_gpio_config_in,
    .config_out = hi3660_gpio_config_out,
    .set_alt_function = hi3660_gpio_set_alt_function,
    .read = hi3660_gpio_read,
    .write = hi3660_gpio_write,
    .get_interrupt = hi3660_gpio_get_interrupt,
    .release_interrupt = hi3660_gpio_release_interrupt,
    .set_polarity = hi3660_gpio_set_polarity,
};

typedef struct {
  uint32_t start_pin;
  uint32_t pin_count;
  const uint32_t* irqs;
  uint32_t irq_count;
} gpio_block_t;

static const gpio_block_t gpio_blocks[] = {
    {
        // GPIO groups 0 - 17
        .start_pin = 0,
        .pin_count = 18 * 8,
    },
    {
        // GPIO groups 18 and 19
        .start_pin = 18 * 8,
        .pin_count = 2 * 8,
    },
    {
        // GPIO groups 20 and 21
        .start_pin = 20 * 8,
        .pin_count = 2 * 8,
    },
    {
        // GPIO groups 22 - 27
        .start_pin = 22 * 8,
        .pin_count = 6 * 8,
    },
    {
        // GPIO group 28
        .start_pin = 28 * 8,
        .pin_count = 1 * 8,
    },
};

void hi3660_gpio_release(void* ctx) {
  hi3660_gpio_t* gpio = ctx;
  pl061_gpios_t* gpios;

  while ((gpios = list_remove_head_type(&gpio->gpios, pl061_gpios_t, node)) != NULL) {
    mmio_buffer_release(&gpios->buffer);
    free(gpios);
  }
  free(gpio);
}

static zx_protocol_device_t gpio_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = hi3660_gpio_release,
};

static zx_status_t hi3660_gpio_bind(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;
  pdev_protocol_t pdev;
  zx_status_t status;

  hi3660_gpio_t* gpio = calloc(1, sizeof(hi3660_gpio_t));
  if (!gpio) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev)) != ZX_OK) {
    zxlogf(ERROR, "hi3660_gpio_bind: ZX_PROTOCOL_PDEV not available");
    return status;
  }
  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus)) != ZX_OK) {
    zxlogf(ERROR, "hi3660_gpio_bind: ZX_PROTOCOL_PBUS not available");
    return status;
  }

  list_initialize(&gpio->gpios);

  for (size_t i = 0; i < countof(gpio_blocks); i++) {
    const gpio_block_t* block = &gpio_blocks[i];

    pl061_gpios_t* gpios = calloc(1, sizeof(pl061_gpios_t));
    if (!gpios) {
      return ZX_ERR_NO_MEMORY;
    }

    status = pdev_map_mmio_buffer(&pdev, i, ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpios->buffer);
    if (status != ZX_OK) {
      zxlogf(ERROR, "pdev_map_mmio_buffer: mmio_buffer_init_physical failed %d", status);
      free(gpios);
      return status;
    }

    // TODO(voydanoff) map interrupts.

    mtx_init(&gpios->lock, mtx_plain);
    gpios->gpio_start = block->start_pin;
    gpios->gpio_count = block->pin_count;
    gpios->irqs = block->irqs;
    gpios->irq_count = block->irq_count;
    list_add_tail(&gpio->gpios, &gpios->node);
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "hi3660-gpio",
      .ctx = gpio,
      .ops = &gpio_device_proto,
      .proto_id = ZX_PROTOCOL_GPIO_IMPL,
      .proto_ops = &gpio_ops,
  };

  status = device_add(parent, &args, &gpio->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hi3660_gpio_bind: device_add failed");
    return status;
  }

  gpio_impl_protocol_t gpio_proto;
  gpio_proto.ops = &gpio_ops;
  gpio_proto.ctx = gpio;

  return pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto));
}

static zx_driver_ops_t hi3660_gpio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hi3660_gpio_bind,
};

ZIRCON_DRIVER_BEGIN(hi3660_gpio, hi3660_gpio_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_HI3660_GPIO), ZIRCON_DRIVER_END(hi3660_gpio)
