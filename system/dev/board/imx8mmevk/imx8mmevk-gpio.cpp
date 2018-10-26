// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx8mmevk.h"

#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddktl/protocol/gpio-impl.h>
#include <soc/imx8m-mini/imx8m-mini-hw.h>
#include <soc/imx8m-mini/imx8m-mini-iomux.h>

namespace imx8mmevk {

namespace {

const pbus_mmio_t gpio_mmio[] = {
    {
        .base = IMX8MM_AIPS_GPIO1_BASE,
        .length = IMX8MM_AIPS_LENGTH,
    },
    {
        .base = IMX8MM_AIPS_GPIO2_BASE,
        .length = IMX8MM_AIPS_LENGTH,
    },
    {
        .base = IMX8MM_AIPS_GPIO3_BASE,
        .length = IMX8MM_AIPS_LENGTH,
    },
    {
        .base = IMX8MM_AIPS_GPIO4_BASE,
        .length = IMX8MM_AIPS_LENGTH,
    },
    {
        .base = IMX8MM_AIPS_GPIO5_BASE,
        .length = IMX8MM_AIPS_LENGTH,
    },
    {
        .base = IMX8MM_AIPS_IOMUXC_BASE,
        .length = IMX8MM_AIPS_LENGTH,
    },
};

const pbus_irq_t gpio_irq[] = {
    {
        .irq = IMX8MM_A53_INTR_GPIO1_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO1_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO2_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO2_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO3_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO3_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO4_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO4_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO5_INT_COMB_0_15,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IMX8MM_A53_INTR_GPIO5_INT_COMP_16_31,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_dev_t gpio_dev = []() {
    pbus_dev_t ret;
    ret.name = "gpio";
    ret.vid = PDEV_VID_NXP;
    ret.pid = PDEV_PID_IMX8MMEVK;
    ret.did = PDEV_DID_IMX_GPIO;
    ret.mmio_list = gpio_mmio;
    ret.mmio_count = countof(gpio_mmio);
    ret.irq_list = gpio_irq;
    ret.irq_count = countof(gpio_irq);
    return ret;
}();

const iomux_cfg_struct iomux[] = {
    /*
     * 40-pin GPIO header pinmux config
     *
     * Pin  Name      Soc-pad       Mode   Soc-port       Notes
     * ----------------------------------------------------------------------
     * 1    3.3V      -             -      -              Hard wired
     * 2    5V        -             -      -              Hard Wired
     * 3    SDA.1     I2C3_SDA      ALT0   ICC3.SDA       (see note1)
     * 4    5V        -             -      -              Hard wired
     * 5    SCL.1     I2C3_SCL      ALT0   I2C3.SCL       (see note1)
     * 6    GND       -             -      -              Hard wired
     * 7    GPIO.7    ECSPI1_MISO   ALT5   GPIO5.IO[8]    -
     * 8    TXD       UART3_TXD     ALT5   GPIO5.IO[27]   -
     * 9    GND       -             -      -              Hard wired
     * 10   RXD       UART3_RXD     ALT5   GPIO5.IO[26]   -
     * 11   GPIO.0    ECSPI1_SS0    ALT5   GPIO5.IO[9]    -
     * 12   GPIO.1    (expander)    -      -              EXP_IO8 (see note1)
     * 13   GPIO.2    (expander)    -      -              EXP_IO9
     * 14   GND       -             -      -              Hard wired
     * 15   GPIO.3    (expander)    -      -              EXP_IO10
     * 16   GPIO.4    (expander)    -      -              EXP_IO11
     * 17   3.3V      -             -      -              Hard wired
     * 18   GPIO.5    -             -      -              Left floating
     * 19   MOSI      ECSPI2_MOSI   ALT5   GPIO5.IO[11]   -
     * 20   GND       -             -      -              Hard wired
     * 21   MISO      ECSPI2_MISO   ALT5   GPIO5.IO[12]   -
     * 22   GPIO.6    -             -      -              Left floating
     * 23   SCLK      ECSPI2_SCLK   ALT5   GPIO5.IO[10]   -
     * 24   CE0       ECSPI2_SS0    ALT5   GPIO5.IO[13]   -
     * 25   GND       -             -      -              Hard wired
     * 26   CE1       -             -      -              Left floating
     * 27   SDA.0     -             -      -              Left floating
     * 28   SCL.0     -             -      -              Left floating
     * 29   GPIO.21   -             -      -              Left floating
     * 30   GND       -             -      -              Hard wired
     * 31   GPIO.22   (expander)    -      -              EXP_IO14
     * 32   GPIO.26   (expander)    -      -              EXP_IO12
     * 33   GPIO.23   (expander)    -      -              EXP_IO13
     * 34   GND       -             -      -              Hard wired
     * 35   GPIO.24   SAI5_RXD3     ALT5   GPIO3.IO[24]   -
     * 36   GPIO.27   SAI5_RXD2     ALT5   GPIO3.IO[23]   -
     * 37   GPIO.25   SAI5_RXD1     ALT5   GPIO3.IO[22]   -
     * 38   GPIO.28   SAI5_RXD0     ALT5   GPIO3.IO[21]   -
     * 39   GND       -             -      -              Hard wired
     * 40   GPIO.29   SAI5_RXC      ALT5   GPIO3.IO[20]   -
     *
     * note1 - The SoC is attached to an I2C-connected IO-expander IC (U201).  Some gpio header pins
     * are routed to this expander IC and not to SoC pads.  The IO-expander's clock and data lines
     * are also routed to the gpio header.
     */

    /* PINMUX config to match the above table */
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_ECSPI1_MISO),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_UART3_TXD),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_UART3_RXD),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_ECSPI1_SS0),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_ECSPI2_MOSI),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_ECSPI2_MISO),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_ECSPI2_SCLK),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_ECSPI2_SS0),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_SAI5_RXD3),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_SAI5_RXD2),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_SAI5_RXD1),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_SAI5_RXD0),
    MAKE_PIN_CFG_DEFAULT(5, SW_MUX_CTL_PAD_SAI5_RXC),

    /* PINMUX config to enable I2C between the IO-expander IC (U201) */
    MAKE_PIN_CFG_DEFAULT(0, SW_MUX_CTL_PAD_I2C3_SDA),
    MAKE_PIN_CFG_DEFAULT(0, SW_MUX_CTL_PAD_I2C3_SCL),
};

} // namespace

zx_status_t Board::StartGpio() {
    auto status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
    if (status != ZX_OK) {
        ERROR("ProtocolDeviceAdd() error: %d\n", status);
        return status;
    }

    status = device_get_protocol(parent(), ZX_PROTOCOL_GPIO_IMPL, &gpio_impl_);
    if (status != ZX_OK) {
        ERROR("GetProtocol() error: %d\n", status);
        return status;
    }

    ddk::GpioImplProtocolProxy gpio(&gpio_impl_);
    for (const auto& mux : iomux) {
        status = gpio.SetAltFunction(0, mux);
        if (status != ZX_OK) {
            iomux_cfg_struct mode = GET_MUX_MODE_VAL(mux);
            iomux_cfg_struct ctl_off = GET_MUX_CTL_OFF_VAL(mux);
            ERROR("could not set pad ctl (0x%04lx) to ALT%01lu\n", mode, ctl_off);
        }
    }

    return ZX_OK;
}

} // namespace imx8mmevk
