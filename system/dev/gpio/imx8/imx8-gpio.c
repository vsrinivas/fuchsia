// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <threads.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/syscalls/port.h>

#include <soc/imx8m/imx8m.h>
#include <soc/imx8m/imx8m-hw.h>
#include <soc/imx8m/imx8m-iomux.h>
#include <soc/imx8m/imx8m-gpio.h>
#include <hw/reg.h>
#include <zircon/assert.h>
#include <zircon/types.h>

typedef struct {
    platform_device_protocol_t  pdev;
    platform_bus_protocol_t     pbus;
    gpio_protocol_t             gpio;
    zx_device_t*                zxdev;
    io_buffer_t                 mmios[IMX_GPIO_BLOCKS];
    io_buffer_t                 mmio_iomux;
    mtx_t                       lock[IMX_GPIO_BLOCKS];
    zx_handle_t                 inth[IMX_GPIO_INTERRUPTS];
    zx_handle_t                 vinth[IMX_GPIO_MAX];
    zx_handle_t                 porth;
    thrd_t                      irq_handler;
    mtx_t                       gpio_lock;
} imx8_gpio_t;


#define READ32_GPIO_REG(block_index, offset)         \
                                  readl(io_buffer_virt(&gpio->mmios[block_index]) + offset)
#define WRITE32_GPIO_REG(block_index, offset, value) writel(value, \
                                  io_buffer_virt(&gpio->mmios[block_index]) + offset)

static zx_status_t imx8_gpio_config(void* ctx, uint32_t pin, uint32_t flags) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    uint32_t regVal;
    imx8_gpio_t* gpio = ctx;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);

    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= 32) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
            __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&gpio->lock[gpio_block]);
    regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_GDIR);
    regVal &= ~(1 << gpio_pin);
    uint32_t direction = flags & GPIO_DIR_MASK;

    if (direction & GPIO_DIR_OUT) {
        regVal |= (GPIO_OUTPUT << gpio_pin);
    } else {
        regVal |= (GPIO_INPUT << gpio_pin);
    }
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_GDIR, regVal);
    mtx_unlock(&gpio->lock[gpio_block]);
    return ZX_OK;
}


static zx_status_t imx8_gpio_read(void* ctx, uint32_t pin, uint8_t* out_value) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    uint32_t regVal;
    imx8_gpio_t* gpio = ctx;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);

    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= 32) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
            __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&gpio->lock[gpio_block]);
    regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_DR);
    regVal >>= (gpio_pin);
    regVal &= 1;
    *out_value = regVal;
    mtx_unlock(&gpio->lock[gpio_block]);

    return ZX_OK;
}

static zx_status_t imx8_gpio_write(void* ctx, uint32_t pin, uint8_t value) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    uint32_t regVal;
    imx8_gpio_t* gpio = ctx;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);
    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= 32) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
            __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&gpio->lock[gpio_block]);
    regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_DR);
    regVal &= ~(1 << gpio_pin);
    regVal |= (value << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_DR, regVal);
    mtx_unlock(&gpio->lock[gpio_block]);


    return ZX_OK;
}

// Configure a pin for an alternate function specified by fn
static zx_status_t imx8_gpio_set_alt_function(void* ctx, const uint32_t pin, const uint64_t fn) {
    imx8_gpio_t* gpio = ctx;
    iomux_cfg_struct s_cfg = (iomux_cfg_struct) fn;

    volatile uint8_t* iomux = (volatile uint8_t*)io_buffer_virt(&gpio->mmio_iomux);

    zxlogf(SPEW, "0x%lx\n", s_cfg);
    zxlogf(SPEW, "val = 0x%lx, reg = %p\n",
                IOMUX_CFG_MUX_MODE_VAL(GET_MUX_MODE_VAL(s_cfg)) |
                IOMUX_CFG_SION_VAL(GET_SION_VAL(s_cfg)),
                iomux + GET_MUX_CTL_OFF_VAL(s_cfg));
    zxlogf(SPEW, "val = 0x%lx, reg = %p\n",
                IOMUX_CFG_DSE_VAL(GET_DSE_VAL(s_cfg)) |
                IOMUX_CFG_SRE_VAL(GET_SRE_VAL(s_cfg)) |
                IOMUX_CFG_ODE_VAL(GET_ODE_VAL(s_cfg)) |
                IOMUX_CFG_PUE_VAL(GET_PUE_VAL(s_cfg)) |
                IOMUX_CFG_HYS_VAL(GET_HYS_VAL(s_cfg)) |
                IOMUX_CFG_LVTTL_VAL(GET_LVTTL_VAL(s_cfg)) |
                IOMUX_CFG_VSEL_VAL(GET_VSEL_VAL(s_cfg)),
                iomux + GET_PAD_CTL_OFF_VAL(s_cfg));
    zxlogf(SPEW, "val = 0x%lx, reg = %p\n",
                IOMUX_CFG_DAISY_VAL(GET_DAISY_VAL(s_cfg)),
                iomux + GET_SEL_INP_OFF_VAL(s_cfg));

    if (GET_MUX_CTL_OFF_VAL(s_cfg)) {
        writel(
                IOMUX_CFG_MUX_MODE_VAL(GET_MUX_MODE_VAL(s_cfg)) |
                IOMUX_CFG_SION_VAL(GET_SION_VAL(s_cfg)),
                    iomux + GET_MUX_CTL_OFF_VAL(s_cfg));
    }
    if (GET_PAD_CTL_OFF_VAL(s_cfg)) {
        writel(
                IOMUX_CFG_DSE_VAL(GET_DSE_VAL(s_cfg)) |
                IOMUX_CFG_SRE_VAL(GET_SRE_VAL(s_cfg)) |
                IOMUX_CFG_ODE_VAL(GET_ODE_VAL(s_cfg)) |
                IOMUX_CFG_PUE_VAL(GET_PUE_VAL(s_cfg)) |
                IOMUX_CFG_HYS_VAL(GET_HYS_VAL(s_cfg)) |
                IOMUX_CFG_LVTTL_VAL(GET_LVTTL_VAL(s_cfg)) |
                IOMUX_CFG_VSEL_VAL(GET_VSEL_VAL(s_cfg)),
                    iomux + GET_PAD_CTL_OFF_VAL(s_cfg));
    }
    if (GET_SEL_INP_OFF_VAL(s_cfg)) {
        writel(IOMUX_CFG_DAISY_VAL(GET_DAISY_VAL(s_cfg)),
                    iomux + GET_SEL_INP_OFF_VAL(s_cfg));
    }

    return ZX_OK;
}

static void imx8_gpio_mask_irq(imx8_gpio_t *gpio, uint32_t gpio_block, uint32_t gpio_pin) {
    uint32_t regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_IMR);
    regVal &= ~(1 << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_IMR, regVal);
}

static void imx8_gpio_unmask_irq(imx8_gpio_t *gpio, uint32_t gpio_block, uint32_t gpio_pin) {
    uint32_t regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_IMR);
    regVal |= (1 << gpio_pin);
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_IMR, regVal);
}

static int imx8_gpio_irq_handler(void *arg) {
    imx8_gpio_t* gpio = arg;
    zx_port_packet_t packet;
    zx_status_t status = ZX_OK;
    uint32_t gpio_block;
    uint32_t isr;
    uint32_t imr;
    uint32_t pin;

    while(1) {
        status = zx_port_wait(gpio->porth, ZX_TIME_INFINITE, &packet);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: zx_port_wait failed %d \n", __FUNCTION__, status);
            goto fail;
        }
        zxlogf(INFO, "GPIO Interrupt %x triggered\n", (unsigned int)packet.key);
        status = zx_interrupt_ack(gpio->inth[packet.key]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: zx_interrupt_ack failed %d \n", __FUNCTION__, status);
            goto fail;
        }

        gpio_block = IMX_INT_NUM_TO_BLOCK(packet.key);
        isr = READ32_GPIO_REG(gpio_block, IMX_GPIO_ISR);

        imr = READ32_GPIO_REG(gpio_block, IMX_GPIO_IMR);

        // Get the status of the enabled interrupts
        // Get the last valid interrupt pin
        uint32_t valid_irqs = (isr & imr);
        if (valid_irqs) {
            pin = __builtin_ctz(valid_irqs);
            WRITE32_GPIO_REG(gpio_block, IMX_GPIO_ISR, 1 << pin);
            pin = gpio_block*IMX_GPIO_PER_BLOCK + pin;

            // Trigger the corresponding virtual interrupt
            status = zx_interrupt_trigger(gpio->vinth[pin], 0, zx_clock_get_monotonic());
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s: zx_interrupt_trigger failed %d \n", __FUNCTION__, status);
                goto fail;
            }
        }
    }

fail:
    for (int i=0; i<IMX_GPIO_INTERRUPTS; i++) {
        zx_interrupt_destroy(gpio->inth[i]);
        zx_handle_close(gpio->inth[i]);
    }
    return status;
}

static zx_status_t imx8_gpio_get_interrupt(void *ctx, uint32_t pin,
                                          uint32_t flags,
                                          zx_handle_t *out_handle) {
    uint32_t gpio_block;
    uint32_t gpio_pin;
    imx8_gpio_t* gpio = ctx;
    uint32_t regVal;
    uint32_t interrupt_type;
    zx_status_t status = ZX_OK;
    uint32_t icr_offset;

    gpio_block = IMX_NUM_TO_BLOCK(pin);
    gpio_pin = IMX_NUM_TO_BIT(pin);
    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= IMX_GPIO_PER_BLOCK) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
            __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }

    // Create Virtual Interrupt
    status = zx_interrupt_create(0, 0, ZX_INTERRUPT_VIRTUAL, &gpio->vinth[pin]);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_irq_create failed %d \n", __FUNCTION__, status);
        return status;
    }

    // Store the Virtual Interrupt
    status = zx_handle_duplicate(gpio->vinth[pin], ZX_RIGHT_SAME_RIGHTS, out_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_handle_duplicate failed %d \n", __FUNCTION__, status);
        return status;
    }

    mtx_lock(&gpio->lock[gpio_block]);
    // Select EGDE or LEVEL and polarity
    switch (flags & ZX_INTERRUPT_MODE_MASK) {
        case ZX_INTERRUPT_MODE_EDGE_LOW:
            interrupt_type = IMX_GPIO_FALLING_EDGE_INTERRUPT;
            break;
        case ZX_INTERRUPT_MODE_EDGE_HIGH:
            interrupt_type = IMX_GPIO_RISING_EDGE_INTERRUPT;
            break;
        case ZX_INTERRUPT_MODE_LEVEL_LOW:
            interrupt_type = IMX_GPIO_LOW_LEVEL_INTERRUPT;
            break;
        case ZX_INTERRUPT_MODE_LEVEL_HIGH:
            interrupt_type = IMX_GPIO_HIGH_LEVEL_INTERRUPT;
            break;
        case ZX_INTERRUPT_MODE_EDGE_BOTH:
            interrupt_type = IMX_GPIO_BOTH_EDGE_INTERRUPT;
            break;
        default:
            status = ZX_ERR_INVALID_ARGS;
            goto fail;
    }

    if (interrupt_type == IMX_GPIO_BOTH_EDGE_INTERRUPT) {
        regVal = READ32_GPIO_REG(gpio_block, IMX_GPIO_EDGE_SEL);
        regVal |= (1 << gpio_pin);
        WRITE32_GPIO_REG(gpio_block, IMX_GPIO_EDGE_SEL, regVal);
    } else {
    // Select which ICR register to program
        if (gpio_pin >= IMX_GPIO_MAX_ICR_PIN) {
            icr_offset = IMX_GPIO_ICR2;
        } else {
            icr_offset = IMX_GPIO_ICR1;
        }
        regVal = READ32_GPIO_REG(gpio_block, icr_offset);
        regVal &= ~(IMX_GPIO_ICR_MASK << IMX_GPIO_ICR_SHIFT(gpio_pin));
        regVal |= (interrupt_type << IMX_GPIO_ICR_SHIFT(gpio_pin));
        WRITE32_GPIO_REG(gpio_block, icr_offset, regVal);
    }

    // Mask the Interrupt
    imx8_gpio_mask_irq(gpio, gpio_block, gpio_pin);

    // Clear the Interrupt Status
    WRITE32_GPIO_REG(gpio_block, IMX_GPIO_ISR, 1 << gpio_pin);

    // Unmask the Interrupt
    imx8_gpio_unmask_irq(gpio, gpio_block, gpio_pin);

fail:
    mtx_unlock(&gpio->lock[gpio_block]);
    return status;
}

static zx_status_t imx8_gpio_release_interrupt(void *ctx, uint32_t pin) {
    imx8_gpio_t* gpio = ctx;
    zx_status_t status = ZX_OK;
    uint32_t gpio_pin = IMX_NUM_TO_BIT(pin);
    uint32_t gpio_block = IMX_NUM_TO_BLOCK(pin);
    if (gpio_block >= IMX_GPIO_BLOCKS || gpio_pin >= IMX_GPIO_PER_BLOCK) {
        zxlogf(ERROR, "%s: Invalid GPIO pin (pin = %d Block = %d, Offset = %d)\n",
            __FUNCTION__, pin, gpio_block, gpio_pin);
        return ZX_ERR_INVALID_ARGS;
    }
    mtx_lock(&gpio->gpio_lock);
    // Mask the interrupt
    imx8_gpio_mask_irq(gpio, gpio_block, gpio_pin);

    status = zx_handle_close(gpio->vinth[pin]);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_handle_close failed %d \n", __FUNCTION__, status);
        goto fail;
    }

    gpio->vinth[pin] = 0;

fail:
    mtx_unlock(&gpio->gpio_lock);
    return status;
}
static gpio_protocol_ops_t gpio_ops = {
    .config = imx8_gpio_config,
    .set_alt_function = imx8_gpio_set_alt_function,
    .read = imx8_gpio_read,
    .write = imx8_gpio_write,
    .get_interrupt = imx8_gpio_get_interrupt,
    .release_interrupt = imx8_gpio_release_interrupt,
};

static void imx8_gpio_release(void* ctx)
{
    unsigned i;
    imx8_gpio_t* gpio = ctx;
    mtx_lock(&gpio->gpio_lock);
    for (i = 0; i < IMX_GPIO_BLOCKS; i++) {
        io_buffer_release(&gpio->mmios[i]);
    }
    io_buffer_release(&gpio->mmio_iomux);

    for (int i=0; i<IMX_GPIO_INTERRUPTS; i++) {
        zx_interrupt_destroy(gpio->inth[i]);
        zx_handle_close(gpio->inth[i]);
    }
    free(gpio);
    mtx_unlock(&gpio->gpio_lock);
}

static zx_protocol_device_t gpio_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = imx8_gpio_release,
};

static zx_status_t imx8_gpio_bind(void* ctx, zx_device_t* parent)
{
    zx_status_t status;
    unsigned i;

    imx8_gpio_t* gpio = calloc(1, sizeof(imx8_gpio_t));
    if (!gpio) {
        return ZX_ERR_NO_MEMORY;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &gpio->pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_DEV not available %d \n", __FUNCTION__, status);
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &gpio->pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_BUS not available %d\n", __FUNCTION__, status);
        goto fail;
    }

    for (i = 0; i < IMX_GPIO_BLOCKS; i++) {
        status = pdev_map_mmio_buffer(&gpio->pdev, i, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                        &gpio->mmios[i]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: pdev_map_mmio_buffer gpio failed %d\n", __FUNCTION__, status);
            goto fail;
        }

        mtx_init(&gpio->lock[i], mtx_plain);
    }

    status = pdev_map_mmio_buffer(&gpio->pdev, IMX_GPIO_BLOCKS, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                    &gpio->mmio_iomux);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer iomux failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&gpio->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_get_device_info failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    status = zx_port_create(1/*PORT_BIND_TO_INTERRUPT*/, &gpio->porth);
    if (status != ZX_OK) {
            zxlogf(ERROR, "%s: zx_port_create failed %d\n", __FUNCTION__, status);
            goto fail;
    }

    for (i=0; i<info.irq_count; i++) {
        // Create Interrupt Object
        status = pdev_map_interrupt(&gpio->pdev, i,
                                    &gpio->inth[i]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: pdev_map_interrupt failed %d\n", __FUNCTION__, status);
            goto fail;
        }
        // The KEY is the Interrupt Number for our usecase
        status = zx_interrupt_bind(gpio->inth[i], gpio->porth, i, 0/*optons*/);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: zx_interrupt_bind failed %d\n", __FUNCTION__, status);
            goto fail;
        }
    }

    thrd_create_with_name(&gpio->irq_handler, imx8_gpio_irq_handler, gpio, "imx8_gpio_irq_handler");

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "imx8-gpio",
        .ctx = gpio,
        .ops = &gpio_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &gpio->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: device_add failed! %d\n", __FUNCTION__, status);
        goto fail;
    }

    gpio->gpio.ops = &gpio_ops;
    gpio->gpio.ctx = gpio;
    pbus_set_protocol(&gpio->pbus, ZX_PROTOCOL_GPIO, &gpio->gpio);

    return ZX_OK;

fail:
    imx8_gpio_release(gpio);
    return status;

}

static zx_driver_ops_t imx8_gpio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = imx8_gpio_bind,
};

ZIRCON_DRIVER_BEGIN(imx8_gpio, imx8_gpio_driver_ops, "zircon", "0.1", 6)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_IMX_GPIO),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_IMX8MEVK),
ZIRCON_DRIVER_END(imx8_gpio)
