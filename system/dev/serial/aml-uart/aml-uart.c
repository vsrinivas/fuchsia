// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <threads.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/serial.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-uart.h>

#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <string.h>

// crystal clock speed
#define CLK_XTAL 24000000

// default configuration for the case that serial_impl_config is not called
#define DEFAULT_BAUD_RATE 115200
#define DEFAULT_CONFIG    (SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 | SERIAL_PARITY_NONE)

// generate interrupt if TX buffer drops below half full
#define XMIT_IRQ_COUNT 32
// generate interrupt as soon as we receive any data
#define RECV_IRQ_COUNT 1

#define INTERRUPT_THRESHOLDS ((XMIT_IRQ_COUNT <<  AML_UART_MISC_XMIT_IRQ_COUNT_SHIFT) | \
                              (RECV_IRQ_COUNT << AML_UART_MISC_RECV_IRQ_COUNT_SHIFT))

typedef struct {
    platform_device_protocol_t pdev;
    serial_impl_protocol_t serial;
    zx_device_t* zxdev;
    serial_port_info_t serial_port_info;
    io_buffer_t mmio;
    thrd_t irq_thread;
    zx_handle_t irq_handle;
    serial_notify_cb notify_cb;
    void* notify_cb_cookie;
    uint32_t state; // last state we sent to notify_cb
    bool enabled;

    mtx_t enable_lock;  // protects enabling/disabling lifecycle
    mtx_t status_lock;  // protects status register and notify_cb
} aml_uart_t;

// reads the current state from the status register and calls notify_cb if it has changed
static uint32_t aml_uart_read_state(aml_uart_t* uart) {
    void* mmio = io_buffer_virt(&uart->mmio);

    mtx_lock(&uart->status_lock);

    uint32_t status = readl(mmio + AML_UART_STATUS);

    uint32_t state = 0;
    if (!(status & AML_UART_STATUS_RXEMPTY)) {
        state |= SERIAL_STATE_READABLE;
    }
    if (!(status & AML_UART_STATUS_TXFULL)) {
        state |= SERIAL_STATE_WRITABLE;
    }
    bool notify = (state != uart->state);
    uart->state = state;

    if (notify && uart->notify_cb) {
        uart->notify_cb(state, uart->notify_cb_cookie);
    }

    mtx_unlock(&uart->status_lock);

    return state;
}

static int aml_uart_irq_thread(void *arg) {
    zxlogf(INFO, "aml_uart_irq_thread start\n");

    aml_uart_t* uart = arg;

    while (1) {
        uint64_t slots;
        zx_status_t result = zx_interrupt_wait(uart->irq_handle, &slots);
        if (result != ZX_OK) {
            zxlogf(ERROR, "aml_uart_irq_thread: zx_interrupt_wait got %d\n", result);
            break;
        }
        if (slots & (1ul << ZX_INTERRUPT_SLOT_USER)) {
            break;
        }

        // this will call the notify_cb if the serial state has changed
        aml_uart_read_state(uart);
    }

    return 0;
}

static zx_status_t aml_serial_get_info(void* ctx, serial_port_info_t* info) {
    aml_uart_t* uart = ctx;
    memcpy(info, &uart->serial_port_info, sizeof(*info));
    return ZX_OK;
}

static zx_status_t aml_serial_config(void* ctx, uint32_t baud_rate, uint32_t flags) {
    aml_uart_t* uart = ctx;
    void* mmio = io_buffer_virt(&uart->mmio);

    // control register is determined completely by this logic, so start with a clean slate
    uint32_t ctrl_bits = 0;

    if ((flags & SERIAL_SET_BAUD_RATE_ONLY) == 0) {
        switch (flags & SERIAL_DATA_BITS_MASK) {
        case SERIAL_DATA_BITS_5:
            ctrl_bits |= AML_UART_CONTROL_XMITLEN_5;
            break;
        case SERIAL_DATA_BITS_6:
            ctrl_bits |= AML_UART_CONTROL_XMITLEN_6;
            break;
        case SERIAL_DATA_BITS_7:
            ctrl_bits |= AML_UART_CONTROL_XMITLEN_7;
            break;
        case SERIAL_DATA_BITS_8:
            ctrl_bits |= AML_UART_CONTROL_XMITLEN_8;
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }

        switch (flags & SERIAL_STOP_BITS_MASK) {
        case SERIAL_STOP_BITS_1:
            ctrl_bits |= AML_UART_CONTROL_STOPLEN_1;
            break;
        case SERIAL_STOP_BITS_2:
            ctrl_bits |= AML_UART_CONTROL_STOPLEN_2;
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }

        switch (flags & SERIAL_PARITY_MASK) {
        case SERIAL_PARITY_NONE:
            ctrl_bits |= AML_UART_CONTROL_PAR_NONE;
            break;
        case SERIAL_PARITY_EVEN:
            ctrl_bits |= AML_UART_CONTROL_PAR_EVEN;
            break;
        case SERIAL_PARITY_ODD:
            ctrl_bits |= AML_UART_CONTROL_PAR_ODD;
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }

        switch (flags & SERIAL_FLOW_CTRL_MASK) {
        case SERIAL_FLOW_CTRL_NONE:
            ctrl_bits |= AML_UART_CONTROL_TWOWIRE;
            break;
        case SERIAL_FLOW_CTRL_CTS_RTS:
            // CTS/RTS is on by default
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }
    }

    // configure baud rate based on CLK_XTAL
    // see meson_uart_change_speed() in drivers/amlogic/uart/uart/meson_uart.c
    uint32_t baud_bits = (CLK_XTAL / 3) / baud_rate - 1;
    if (baud_bits & ~AML_UART_REG5_NEW_BAUD_RATE_MASK) {
        zxlogf(ERROR, "aml_serial_config: baud rate %u too large\n", baud_rate);
        return ZX_ERR_OUT_OF_RANGE;
    }
    baud_bits |= AML_UART_REG5_USE_XTAL_CLK | AML_UART_REG5_USE_NEW_BAUD_RATE;

    mtx_lock(&uart->enable_lock);

    if ((flags & SERIAL_SET_BAUD_RATE_ONLY) == 0) {
        // invert our RTS if we are we are not enabled and configured for flow control
        if (!uart->enabled && (ctrl_bits & AML_UART_CONTROL_TWOWIRE) == 0) {
            ctrl_bits |= AML_UART_CONTROL_INVRTS;
        }

        writel(ctrl_bits, mmio + AML_UART_CONTROL);
    }

    writel(baud_bits, mmio + AML_UART_REG5);

    mtx_unlock(&uart->enable_lock);

    return ZX_OK;
}

static void aml_serial_enable_locked(aml_uart_t* uart, bool enable) {
    void* mmio = io_buffer_virt(&uart->mmio);
    volatile uint32_t* ctrl_reg = mmio + AML_UART_CONTROL;
    volatile uint32_t* misc_reg = mmio + AML_UART_MISC;

    uint32_t ctrl = readl(ctrl_reg);

    if (enable) {
        // reset the port
        ctrl |= AML_UART_CONTROL_RSTRX | AML_UART_CONTROL_RSTTX | AML_UART_CONTROL_CLRERR;
        writel(ctrl, ctrl_reg);
        ctrl &= ~(AML_UART_CONTROL_RSTRX | AML_UART_CONTROL_RSTTX | AML_UART_CONTROL_CLRERR);
        writel(ctrl, ctrl_reg);

        // enable rx and tx
        ctrl |= AML_UART_CONTROL_TXEN | AML_UART_CONTROL_RXEN;
        ctrl |= AML_UART_CONTROL_TXINTEN | AML_UART_CONTROL_RXINTEN;
        // clear our RTS
        ctrl &= ~AML_UART_CONTROL_INVRTS;
        writel(ctrl, ctrl_reg);

        // Set interrupt thresholds
        static_assert((INTERRUPT_THRESHOLDS & ~(AML_UART_MISC_XMIT_IRQ_COUNT_MASK |
                                                AML_UART_MISC_RECV_IRQ_COUNT_MASK)) == 0, "");
        writel(INTERRUPT_THRESHOLDS, misc_reg);
    } else {
        ctrl &= ~(AML_UART_CONTROL_TXEN | AML_UART_CONTROL_RXEN);

        // invert our RTS if we are configured for flow control
        if ((ctrl & AML_UART_CONTROL_TWOWIRE) == 0) {
            ctrl |= AML_UART_CONTROL_INVRTS;
        }

        writel(ctrl, ctrl_reg);
    }
}

static zx_status_t aml_serial_enable(void* ctx, bool enable) {
    aml_uart_t* uart = ctx;

    mtx_lock(&uart->enable_lock);

    if (enable && !uart->enabled) {
        zx_status_t status = pdev_map_interrupt(&uart->pdev, 0, &uart->irq_handle);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_serial_enable: pdev_map_interrupt failed %d\n", status);
            mtx_unlock(&uart->enable_lock);
            return status;
        }

        aml_serial_enable_locked(uart, true);

        int rc = thrd_create_with_name(&uart->irq_thread, aml_uart_irq_thread, uart,
                                       "aml_uart_irq_thread");
        if (rc != thrd_success) {
            aml_serial_enable_locked(uart, false);
            zx_handle_close(uart->irq_handle);
            uart->irq_handle = ZX_HANDLE_INVALID;
            mtx_unlock(&uart->enable_lock);
            return thrd_status_to_zx_status(rc);
        }
    } else if (!enable && uart->enabled) {
        zx_interrupt_signal(uart->irq_handle, ZX_INTERRUPT_SLOT_USER, 0);
        thrd_join(uart->irq_thread, NULL);
        aml_serial_enable_locked(uart, false);
        zx_handle_close(uart->irq_handle);
        uart->irq_handle = ZX_HANDLE_INVALID;
    }

    uart->enabled = enable;
    mtx_unlock(&uart->enable_lock);

    return ZX_OK;
}

static zx_status_t aml_serial_read(void* ctx, void* buf, size_t length,
                                   size_t* out_actual) {
    aml_uart_t* uart = ctx;
    void* mmio = io_buffer_virt(&uart->mmio);
    volatile uint32_t* rfifo_reg = mmio + AML_UART_RFIFO;

    uint8_t* bufptr = buf;
    uint8_t* end = bufptr + length;
    while (bufptr < end && (aml_uart_read_state(uart) & SERIAL_STATE_READABLE)) {
        uint32_t val = readl(rfifo_reg);
        *bufptr++ = val;
    }

    size_t read = (void *)bufptr - buf;
    *out_actual = read;
    if (read == 0) {
        return ZX_ERR_SHOULD_WAIT;
    }
    return ZX_OK;
}

static zx_status_t aml_serial_write(void* ctx, const void* buf, size_t length,
                                    size_t* out_actual) {
    aml_uart_t* uart = ctx;
    void* mmio = io_buffer_virt(&uart->mmio);
    volatile uint32_t* wfifo_reg = mmio + AML_UART_WFIFO;

    const uint8_t* bufptr = buf;
    const uint8_t* end = bufptr + length;
    while (bufptr < end && (aml_uart_read_state(uart) & SERIAL_STATE_WRITABLE)) {
        writel(*bufptr++, wfifo_reg);
    }

    size_t written = (void *)bufptr - buf;
    *out_actual = written;
    if (written == 0) {
        return ZX_ERR_SHOULD_WAIT;
    }
    return ZX_OK;
}

static zx_status_t aml_serial_set_notify_callback(void* ctx, serial_notify_cb cb, void* cookie) {
    aml_uart_t* uart = ctx;

    mtx_lock(&uart->enable_lock);

    if (uart->enabled) {
        zxlogf(ERROR, "aml_serial_set_notify_callback called when driver is enabled\n");
        mtx_unlock(&uart->enable_lock);
        return ZX_ERR_BAD_STATE;
    }

    uart->notify_cb = cb;
    uart->notify_cb_cookie = cookie;

    mtx_unlock(&uart->enable_lock);

    // this will trigger notifying current state
    aml_uart_read_state(uart);

    return ZX_OK;
}

static serial_impl_ops_t aml_serial_ops = {
    .get_info = aml_serial_get_info,
    .config = aml_serial_config,
    .enable = aml_serial_enable,
    .read = aml_serial_read,
    .write = aml_serial_write,
    .set_notify_callback = aml_serial_set_notify_callback,
};

static void aml_uart_release(void* ctx) {
    aml_uart_t* uart = ctx;
    aml_serial_enable(uart, false);
    io_buffer_release(&uart->mmio);
    zx_handle_close(uart->irq_handle);
    free(uart);
}

static zx_protocol_device_t uart_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_uart_release,
};

static zx_status_t aml_uart_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    aml_uart_t* uart = calloc(1, sizeof(aml_uart_t));
    if (!uart) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &uart->pdev)) != ZX_OK) {
        zxlogf(ERROR, "aml_uart_bind: ZX_PROTOCOL_PLATFORM_DEV not available\n");
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&uart->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_uart_bind: pdev_get_device_info failed\n");
        goto fail;
    }
    memcpy(&uart->serial_port_info, &info.serial_port_info, sizeof(uart->serial_port_info));

    mtx_init(&uart->enable_lock, mtx_plain);
    mtx_init(&uart->status_lock, mtx_plain);

    status = pdev_map_mmio_buffer(&uart->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &uart->mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_uart_bind: pdev_map_mmio_buffer failed %d\n", status);
        goto fail;
    }

    aml_serial_config(uart, DEFAULT_BAUD_RATE, DEFAULT_CONFIG);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-uart",
        .ctx = uart,
        .ops = &uart_device_proto,
        .proto_id = ZX_PROTOCOL_SERIAL_IMPL,
        .proto_ops = &aml_serial_ops,
    };

    status = device_add(parent, &args, &uart->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_uart_bind: device_add failed\n");
        goto fail;
    }

    return ZX_OK;

fail:
    aml_uart_release(uart);
    return status;
}

static zx_driver_ops_t aml_uart_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_uart_bind,
};

ZIRCON_DRIVER_BEGIN(aml_uart, aml_uart_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_UART),
ZIRCON_DRIVER_END(aml_uart)
