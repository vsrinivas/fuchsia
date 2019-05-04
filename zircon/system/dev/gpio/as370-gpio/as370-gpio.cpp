// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-gpio.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

namespace {

constexpr zx_off_t kGpioSwPortADr  = 0x00;
constexpr zx_off_t kGpioSwPortADdr = 0x04;
constexpr zx_off_t kGpioExtPortA   = 0x50;

constexpr zx_off_t kPinmuxCntlBusBase = 0x40;

constexpr uint32_t kPorts = 2;
constexpr uint32_t kGpiosPerPort = 32;
constexpr uint32_t kTotalPins = 72;

constexpr uint32_t kPinmuxFunctionWidth = 3;
constexpr uint32_t kPinmuxPinsPerReg = 10;

constexpr uint32_t kGpioPinmuxWindowOffset = 18;

uint32_t GetGpioBitOffset(uint32_t index) {
    return (index < kGpiosPerPort) ? index : (index - kGpiosPerPort);
}

}  // namespace

namespace gpio {

zx_status_t As370Gpio::Create(void* ctx, zx_device_t* parent) {
    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PLATFORM_DEVICE\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    std::optional<ddk::MmioBuffer> pinctrl_mmio;
    zx_status_t status = pdev.MapMmio(0, &pinctrl_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to map pinmux MMIO: %d\n", __FILE__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> gpio1_mmio;
    if ((status = pdev.MapMmio(1, &gpio1_mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to map GPIO 1 MMIO: %d\n", __FILE__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> gpio2_mmio;
    if ((status = pdev.MapMmio(2, &gpio2_mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to map GPIO 2 MMIO: %d\n", __FILE__, status);
        return status;
    }

    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<As370Gpio>(
        &ac, parent, *std::move(pinctrl_mmio), *std::move(gpio1_mmio), *std::move(gpio2_mmio));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Failed to allocate device memory\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device->DdkAdd("as370-gpio")) != ZX_OK) {
        zxlogf(ERROR, "%s: Bind failed: %d\n", __FILE__, status);
        return status;
    }

    status = device->Init();
    __UNUSED auto* dummy = device.release();
    return status;
}

zx_status_t As370Gpio::Init() {
    ddk::PBusProtocolClient pbus(parent());
    if (!pbus.is_valid()) {
        zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PLATFORM_BUS\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    gpio_impl_protocol_t gpio_proto = {.ops = &gpio_impl_protocol_ops_, .ctx = this};
    zx_status_t status = pbus.RegisterProtocol(ddk_proto_id_, &gpio_proto, sizeof(gpio_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to register ZX_PROTOCOL_GPIO_IMPL: %d\n", __FILE__, __LINE__);
        return status;
    }

    return ZX_OK;
}

zx_status_t As370Gpio::GpioImplConfigIn(uint32_t index, uint32_t flags) {
    if (index >= kPorts * kGpiosPerPort) {
        return ZX_ERR_OUT_OF_RANGE;
    } else if (flags != GPIO_NO_PULL) {
        // TODO(bradenkell): Add support for enabling pull up/down resistors.
        return ZX_ERR_NOT_SUPPORTED;
    }

    const ddk::MmioBuffer& gpio_mmio = (index < kGpiosPerPort) ? gpio1_mmio_ : gpio2_mmio_;
    gpio_mmio.ClearBit<uint32_t>(GetGpioBitOffset(index), kGpioSwPortADdr);

    return ZX_OK;
}

zx_status_t As370Gpio::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
    if (index >= kPorts * kGpiosPerPort) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    GpioImplWrite(index, initial_value);

    const ddk::MmioBuffer& gpio_mmio = (index < kGpiosPerPort) ? gpio1_mmio_ : gpio2_mmio_;
    gpio_mmio.SetBit<uint32_t>(GetGpioBitOffset(index), kGpioSwPortADdr);

    return ZX_OK;
}

zx_status_t As370Gpio::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
    // The pinmux registers have a gap with respect to the GPIOs, like this:
    // |----- GPIOs 0-17 -----|--- NAND pins ---|--------------- GPIOs 18-63 ---------------|
    // The NAND pins are mapped to GPIOs 63-71, so the index parameter must be adjusted accordingly.

    if (index >= kTotalPins || function > ((1 << kPinmuxFunctionWidth) - 1)) {
        return ZX_ERR_OUT_OF_RANGE;
    } else if (index >= kPorts * kGpiosPerPort) {
        index -= (kPorts * kGpiosPerPort) - kGpioPinmuxWindowOffset;
    } else if (index >= kGpioPinmuxWindowOffset) {
        index += kTotalPins - (kPorts * kGpiosPerPort);
    }

    zx_off_t reg_offset = (sizeof(uint32_t) * (index / kPinmuxPinsPerReg)) + kPinmuxCntlBusBase;
    uint32_t bit_offset = (index % kPinmuxPinsPerReg) * kPinmuxFunctionWidth;
    pinmux_mmio_.ModifyBits<uint32_t>(static_cast<uint32_t>(function), bit_offset,
                                      kPinmuxFunctionWidth, reg_offset);

    return ZX_OK;
}

zx_status_t As370Gpio::GpioImplSetDriveStrength(uint32_t index, uint8_t m_a) {
    // TODO(bradenkell): Implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Gpio::GpioImplRead(uint32_t index, uint8_t* out_value) {
    if (index >= kPorts * kGpiosPerPort) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const ddk::MmioBuffer& gpio_mmio = (index < kGpiosPerPort) ? gpio1_mmio_ : gpio2_mmio_;
    const uint32_t mask = 1 << GetGpioBitOffset(index);
    *out_value = ((gpio_mmio.Read32(kGpioExtPortA) & mask) == 0) ? 0 : 1;

    return ZX_OK;
}

zx_status_t As370Gpio::GpioImplWrite(uint32_t index, uint8_t value) {
    if (index >= kPorts * kGpiosPerPort) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const ddk::MmioBuffer& gpio_mmio = (index < kGpiosPerPort) ? gpio1_mmio_ : gpio2_mmio_;
    gpio_mmio.ModifyBit<uint32_t>(value, GetGpioBitOffset(index), kGpioSwPortADr);

    return ZX_OK;
}

// TODO(bradenkell): Implement these.
zx_status_t As370Gpio::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                            zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Gpio::GpioImplReleaseInterrupt(uint32_t index) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Gpio::GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity) {
    return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace gpio

static zx_driver_ops_t as370_gpio_driver_ops = []() -> zx_driver_ops_t {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = gpio::As370Gpio::Create;
    return ops;
}();

ZIRCON_DRIVER_BEGIN(as370_gpio, as370_gpio_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_SYNAPTICS_GPIO),
ZIRCON_DRIVER_END(as370_gpio)
