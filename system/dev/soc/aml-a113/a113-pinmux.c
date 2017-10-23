// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <threads.h>

#include <bits/limits.h>
#include <ddk/debug.h>
#include <hw/reg.h>

#include <zircon/assert.h>
#include <zircon/types.h>

#include "a113-bus.h"
#include "a113-hw.h"

#define PAGE_MASK (PAGE_SIZE - 1)

// Phsical Base Address for Pinmux/GPIO Control
#define GPIO_BASE_PHYS 0xff634400
#define GPIO_BASE_PAGE ((~PAGE_MASK) & GPIO_BASE_PHYS)

// The GPIO "Always On" Domain has its own control block mapped here.
#define GPIOAO_BASE_PHYS 0xff800000
#define GPIOAO_BASE_PAGE ((~PAGE_MASK) & GPIOAO_BASE_PHYS)

typedef struct pinmux_block {
    uint32_t start_pin;
    uint32_t pin_count;
    uint32_t ctrl_block_offset;
    zx_paddr_t ctrl_block_base_phys;
    zx_vaddr_t ctrl_block_base_virt;
    mtx_t lock;
} pinmux_block_t;

static pinmux_block_t pinmux_blocks[] = {
    // GPIO X Block
    {
        .start_pin = (A113_GPIOX_START + 0),
        .pin_count = 8,
        .ctrl_block_offset = PERIPHS_PIN_MUX_4,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOX_START + 8),
        .pin_count = 8,
        .ctrl_block_offset = PERIPHS_PIN_MUX_5,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOX_START + 16),
        .pin_count = 7,
        .ctrl_block_offset = PERIPHS_PIN_MUX_6,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO A Block
    {
        .start_pin = (A113_GPIOA_START + 0),
        .pin_count = 8,
        .ctrl_block_offset = PERIPHS_PIN_MUX_B,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOA_START + 8),
        .pin_count = 8,
        .ctrl_block_offset = PERIPHS_PIN_MUX_C,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOA_START + 16),
        .pin_count = 5,
        .ctrl_block_offset = PERIPHS_PIN_MUX_D,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO Boot Block
    {
        .start_pin = (A113_GPIOB_START + 0),
        .pin_count = 8,
        .ctrl_block_offset = PERIPHS_PIN_MUX_0,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOB_START + 8),
        .pin_count = 7,
        .ctrl_block_offset = PERIPHS_PIN_MUX_1,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO Y Block
    {
        .start_pin = (A113_GPIOY_START + 0),
        .pin_count = 8,
        .ctrl_block_offset = PERIPHS_PIN_MUX_8,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOY_START + 8),
        .pin_count = 8,
        .ctrl_block_offset = PERIPHS_PIN_MUX_9,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO Z Block
    {
        .start_pin = (A113_GPIOZ_START + 0),
        .pin_count = 8,
        .ctrl_block_offset = PERIPHS_PIN_MUX_2,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOZ_START + 8),
        .pin_count = 3,
        .ctrl_block_offset = PERIPHS_PIN_MUX_3,
        .ctrl_block_base_phys = GPIO_BASE_PAGE,
        .lock = MTX_INIT,
    },

    // GPIO AO Block
    // NOTE: The GPIO AO block has a seperate control block than the other
    //       GPIO blocks.
    {
        .start_pin = (A113_GPIOAO_START + 0),
        .pin_count = 8,
        .ctrl_block_offset = AO_RTI_PIN_MUX_REG0,
        .ctrl_block_base_phys = GPIOAO_BASE_PHYS,
        .lock = MTX_INIT,
    },
    {
        .start_pin = (A113_GPIOAO_START + 8),
        .pin_count = 6,
        .ctrl_block_offset = AO_RTI_PIN_MUX_REG1,
        .ctrl_block_base_phys = GPIOAO_BASE_PHYS,
        .lock = MTX_INIT,
    },
};

static zx_status_t a113_pin_to_block(const uint32_t pinid, pinmux_block_t** result) {
    ZX_DEBUG_ASSERT(result);

    for (size_t i = 0; i < countof(pinmux_blocks); i++) {
        const uint32_t end_pin = pinmux_blocks[i].start_pin + pinmux_blocks[i].pin_count;
        if (pinid >= pinmux_blocks[i].start_pin && pinid < end_pin) {
            *result = &(pinmux_blocks[i]);
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_FOUND;
}

// Configure a pin for an alternate function specified by fn
zx_status_t a113_config_pinmux(void* ctx, const uint32_t pin, const uint32_t fn) {
    ZX_DEBUG_ASSERT(ctx);

    if (fn > A113_PINMUX_ALT_FN_MAX) {
        zxlogf(ERROR, "a113_config_pinmux: pin mux alt config out of range"
                " %u\n", fn);
        return ZX_ERR_OUT_OF_RANGE;
    }

    zx_status_t status;

    pinmux_block_t* block;
    status = a113_pin_to_block(pin, &block);
    if (status != ZX_OK) {
        zxlogf(ERROR, "a113_config_pinmux: pin not found %u\n", pin);
        return status;
    }

    // Points to the control register.
    volatile uint32_t* reg = (volatile uint32_t*)(block->ctrl_block_base_virt);
    reg += block->ctrl_block_offset;

    // Sanity Check: pin_to_block must return a block that contains `pin`
    //               therefore `pin` must be greater than or equal to the first
    //               pin of the block.
    ZX_DEBUG_ASSERT(pin >= block->start_pin);

    // Each Pin Mux is controlled by a 4 bit wide field in `reg`
    // Compute the offset for this pin.
    const uint32_t pin_shift = (pin - block->start_pin) * 4;
    const uint32_t mux_mask = ~(0x0F << pin_shift);
    const uint32_t fn_val = fn << pin_shift;

    mtx_lock(&block->lock);

    uint32_t regval = readl(reg);
    regval &= mux_mask;     // Remove the previous value for the mux
    regval |= fn_val;       // Assign the new value to the mux
    writel(regval, reg);

    mtx_unlock(&block->lock);

    return ZX_OK;
}

zx_status_t a113_init_pinmux(a113_bus_t* bus) {
    ZX_DEBUG_ASSERT(bus);

    zx_handle_t resource = get_root_resource();
    zx_status_t status = ZX_ERR_INTERNAL;

    // Initialize the Standard GPIO Block
    status = io_buffer_init_physical(&bus->periphs_reg, GPIO_BASE_PAGE,
                                     PAGE_SIZE, resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "a113_init_pinmux: Failed to map io buffer at 0x%08x"
                ", status = %d\n", GPIO_BASE_PAGE, status);
        return status;
    }

    // Initialize the "Always On" GPIO AO Block.
    status = io_buffer_init_physical(&bus->periphs_ao_reg, GPIOAO_BASE_PAGE,
                                     PAGE_SIZE, resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "a113_init_pinmux: Failed to map io buffer at 0x%08x"
                ", status = %d\n", GPIOAO_BASE_PAGE, status);

        // Failed to initialize completely. Release the IO Buffer we allocated
        // above.
        io_buffer_release(&bus->periphs_reg);
        return status;
    }

    // Initialize each of the GPIO Pin blocks.
    for (size_t i = 0; i < countof(pinmux_blocks); i++) {
        // Set the appropriate virtual address of the GPIO control block based
        // on the physical address of the block.
        switch(pinmux_blocks[i].ctrl_block_base_phys) {
            case GPIOAO_BASE_PAGE:
                pinmux_blocks[i].ctrl_block_base_virt =
                    ((zx_vaddr_t)io_buffer_virt(&bus->periphs_ao_reg)) +
                    (GPIOAO_BASE_PHYS - GPIOAO_BASE_PAGE);
                break;
            case GPIO_BASE_PAGE:
                pinmux_blocks[i].ctrl_block_base_virt =
                    ((zx_vaddr_t)io_buffer_virt(&bus->periphs_reg)) +
                    (GPIO_BASE_PHYS - GPIO_BASE_PAGE);
                break;
            default:
                zxlogf(ERROR, "a113_init_pinmux: unexpected gpio control block"
                        " base address at 0x%016lx\n",
                        pinmux_blocks[i].ctrl_block_base_phys);
                status = ZX_ERR_NOT_SUPPORTED;
                goto cleanup_and_fail;
        }
    }

    return ZX_OK;

cleanup_and_fail:
    io_buffer_release(&bus->periphs_ao_reg);
    io_buffer_release(&bus->periphs_reg);
    return status;
}