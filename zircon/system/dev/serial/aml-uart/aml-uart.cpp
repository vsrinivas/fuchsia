// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-uart.h"

#include <stdint.h>
#include <string.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/device.h>
#include <hw/reg.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hwreg/mmio.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include "registers.h"

namespace serial {

zx_status_t AmlUart::Create(zx_device_t* parent) {
    zx_status_t status;

    pdev_protocol_t pdev;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev)) != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __func__);
        return status;
    }

    serial_port_info_t info;
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_SERIAL_PORT_INFO, &info, sizeof(info),
                                 &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: device_get_metadata failed %d\n", __func__, status);
        return status;
    }
    if (actual < sizeof(info)) {
        zxlogf(ERROR, "%s: serial_port_info_t metadata too small\n", __func__);
        return ZX_ERR_INTERNAL;
    }

    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer(&pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_&mmio__buffer failed %d\n", __func__, status);
        return status;
    }

    fbl::AllocChecker ac;
    auto* uart = new (&ac) AmlUart(parent, pdev, info, ddk::MmioBuffer(mmio));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto cleanup = fbl::MakeAutoCall([&uart]() { uart->DdkRelease(); });

    // Default configuration for the case that serial_impl_config is not called.
    constexpr uint32_t kDefaultBaudRate = 115200;
    constexpr uint32_t kDefaultConfig =
        SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 | SERIAL_PARITY_NONE;
    uart->SerialImplConfig(kDefaultBaudRate, kDefaultConfig);

    status = uart->DdkAdd("aml-uart");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DdkDeviceAdd failed\n", __func__);
        return status;
    }

    cleanup.cancel();
    return ZX_OK;
}

uint32_t AmlUart::ReadStateAndNotify() {
    fbl::AutoLock al(&status_lock_);

    auto status = Status::Get().ReadFrom(&mmio_);

    uint32_t state = 0;
    if (!status.rx_empty()) {
        state |= SERIAL_STATE_READABLE;
    }
    if (!status.tx_full()) {
        state |= SERIAL_STATE_WRITABLE;
    }
    const bool notify = (state != state_);
    state_ = state;

    if (notify && notify_cb_) {
        notify_cb_(state);
    }

    return state;
}

int AmlUart::IrqThread() {
    zxlogf(INFO, "%s start\n", __func__);

    while (1) {
        zx_status_t status;
        status = irq_.wait(nullptr);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: irq.wait() got %d\n", __func__, status);
            break;
        }
        // This will call the notify_cb if the serial state has changed.
        ReadStateAndNotify();
    }

    return 0;
}

zx_status_t AmlUart::SerialImplGetInfo(serial_port_info_t* info) {
    memcpy(info, &serial_port_info_, sizeof(*info));
    return ZX_OK;
}

zx_status_t AmlUart::SerialImplConfig(uint32_t baud_rate, uint32_t flags) {
    // Control register is determined completely by this logic, so start with a clean slate.
    auto ctrl = Control::Get().FromValue(0);

    if ((flags & SERIAL_SET_BAUD_RATE_ONLY) == 0) {
        switch (flags & SERIAL_DATA_BITS_MASK) {
        case SERIAL_DATA_BITS_5:
            ctrl.set_xmit_len(Control::kXmitLength5);
            break;
        case SERIAL_DATA_BITS_6:
            ctrl.set_xmit_len(Control::kXmitLength6);
            break;
        case SERIAL_DATA_BITS_7:
            ctrl.set_xmit_len(Control::kXmitLength7);
            break;
        case SERIAL_DATA_BITS_8:
            ctrl.set_xmit_len(Control::kXmitLength8);
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }

        switch (flags & SERIAL_STOP_BITS_MASK) {
        case SERIAL_STOP_BITS_1:
            ctrl.set_stop_len(Control::kStopLen1);
            break;
        case SERIAL_STOP_BITS_2:
            ctrl.set_stop_len(Control::kStopLen2);
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }

        switch (flags & SERIAL_PARITY_MASK) {
        case SERIAL_PARITY_NONE:
            ctrl.set_parity(Control::kParityNone);
            break;
        case SERIAL_PARITY_EVEN:
            ctrl.set_parity(Control::kParityEven);
            break;
        case SERIAL_PARITY_ODD:
            ctrl.set_parity(Control::kParityOdd);
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }

        switch (flags & SERIAL_FLOW_CTRL_MASK) {
        case SERIAL_FLOW_CTRL_NONE:
            ctrl.set_two_wire(1);
            break;
        case SERIAL_FLOW_CTRL_CTS_RTS:
            // CTS/RTS is on by default
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }
    }

    // Configure baud rate based on crystal clock speed.
    // See meson_uart_change_speed() in drivers/amlogic/uart/uart/meson_uart.c.
    constexpr uint32_t kCrystalClockSpeed = 24000000;
    uint32_t baud_bits = (kCrystalClockSpeed / 3) / baud_rate - 1;
    if (baud_bits & (~AML_UART_REG5_NEW_BAUD_RATE_MASK)) {
        zxlogf(ERROR, "%s: baud rate %u too large\n", __func__, baud_rate);
        return ZX_ERR_OUT_OF_RANGE;
    }
    auto baud = Reg5::Get()
                    .FromValue(0)
                    .set_new_baud_rate(baud_bits)
                    .set_use_xtal_clk(1)
                    .set_use_new_baud_rate(1);

    fbl::AutoLock al(&enable_lock_);

    if ((flags & SERIAL_SET_BAUD_RATE_ONLY) == 0) {
        // Invert our RTS if we are we are not enabled and configured for flow control.
        if (!enabled_ && (ctrl.two_wire() == 0)) {
            ctrl.set_inv_rts(1);
        }
        ctrl.WriteTo(&mmio_);
    }

    baud.WriteTo(&mmio_);

    return ZX_OK;
}

void AmlUart::EnableLocked(bool enable) {
    auto ctrl = Control::Get().ReadFrom(&mmio_);

    if (enable) {
        // Reset the port.
        ctrl.set_rst_rx(1)
            .set_rst_tx(1)
            .set_clear_error(1)
            .WriteTo(&mmio_);

        ctrl.set_rst_rx(0)
            .set_rst_tx(0)
            .set_clear_error(0)
            .WriteTo(&mmio_);

        // Enable rx and tx.
        ctrl.set_tx_enable(1)
            .set_rx_enable(1)
            .set_tx_interrupt_enable(1)
            .set_rx_interrupt_enable(1)
            // Clear our RTS.
            .set_inv_rts(0)
            .WriteTo(&mmio_);

        // Set interrupt thresholds.
        // Generate interrupt if TX buffer drops below half full.
        constexpr uint32_t kTransmitIrqCount = 32;
        // Generate interrupt as soon as we receive any data.
        constexpr uint32_t kRecieveIrqCount = 1;
        Misc::Get()
            .FromValue(0)
            .set_xmit_irq_count(kTransmitIrqCount)
            .set_recv_irq_count(kRecieveIrqCount)
            .WriteTo(&mmio_);
    } else {
        ctrl.set_tx_enable(0)
            .set_rx_enable(0)
            // Invert our RTS if we are configured for flow control.
            .set_inv_rts(!ctrl.two_wire())
            .WriteTo(&mmio_);
    }
}

zx_status_t AmlUart::SerialImplEnable(bool enable) {
    fbl::AutoLock al(&enable_lock_);

    if (enable && !enabled_) {
        zx_status_t status = pdev_get_interrupt(&pdev_, 0, 0, irq_.reset_and_get_address());
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: pdev_get_interrupt failed %d\n", __func__, status);
            return status;
        }

        EnableLocked(true);

        auto start_thread = [](void* arg) { return static_cast<AmlUart*>(arg)->IrqThread(); };
        int rc = thrd_create_with_name(&irq_thread_, start_thread, this, "aml_uart_irq_thread");
        if (rc != thrd_success) {
            EnableLocked(false);
            return thrd_status_to_zx_status(rc);
        }
    } else if (!enable && enabled_) {
        irq_.destroy();
        thrd_join(irq_thread_, nullptr);
        EnableLocked(false);
    }

    enabled_ = enable;
    return ZX_OK;
}

zx_status_t AmlUart::SerialImplRead(void* buf, size_t length, size_t* out_actual) {
    auto* bufptr = static_cast<uint8_t*>(buf);
    const uint8_t* const end = bufptr + length;
    while (bufptr < end && (ReadStateAndNotify() & SERIAL_STATE_READABLE)) {
        uint32_t val = mmio_.Read32(AML_UART_RFIFO);
        *bufptr++ = static_cast<uint8_t>(val);
    }

    const size_t read = reinterpret_cast<uintptr_t>(bufptr) - reinterpret_cast<uintptr_t>(buf);
    *out_actual = read;
    if (read == 0) {
        return ZX_ERR_SHOULD_WAIT;
    }
    return ZX_OK;
}

zx_status_t AmlUart::SerialImplWrite(const void* buf, size_t length, size_t* out_actual) {
    const auto* bufptr = static_cast<const uint8_t*>(buf);
    const uint8_t* const end = bufptr + length;
    while (bufptr < end && (ReadStateAndNotify() & SERIAL_STATE_WRITABLE)) {
        mmio_.Write32(*bufptr++, AML_UART_WFIFO);
    }

    const size_t written = reinterpret_cast<uintptr_t>(bufptr) - reinterpret_cast<uintptr_t>(buf);
    *out_actual = written;
    if (written == 0) {
        return ZX_ERR_SHOULD_WAIT;
    }
    return ZX_OK;
}

zx_status_t AmlUart::SerialImplSetNotifyCallback(const serial_notify_t* cb) {
    {
        fbl::AutoLock al(&enable_lock_);

        if (enabled_) {
            zxlogf(ERROR, "%s called when driver is enabled\n", __func__);
            return ZX_ERR_BAD_STATE;
        }

        fbl::AutoLock al2(&status_lock_);
        serial_notify_t notify_cb = *cb;
        notify_cb_ = [=](uint32_t state) { notify_cb.callback(notify_cb.ctx, state); };
    }

    // This will trigger notifying current state.
    ReadStateAndNotify();

    return ZX_OK;
}

} // namespace serial

extern "C" zx_status_t aml_uart_bind(void* ctx, zx_device_t* parent) {
    return serial::AmlUart::Create(parent);
}
