/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <lib/fake_ddk/fake_ddk.h>
#include <mock/ddktl/protocol/gpio.h>
#include <mock/ddktl/protocol/sdio.h>
#include <wifi/wifi-config.h>
#include <zxtest/zxtest.h>

#include "../bus.h"
#include "../common.h"
#include "../sdio.h"

// This is required to use ddk::MockSdio.
bool operator==(const sdio_rw_txn_t& lhs, const sdio_rw_txn_t& rhs) {
    return false;
}

namespace {

class MockSdio : public ddk::MockSdio {
public:
    zx_status_t SdioDoVendorControlRwByte(bool write, uint8_t addr, uint8_t write_byte,
                                          uint8_t* out_read_byte) override {
        auto ret = mock_do_vendor_control_rw_byte_.Call(write, addr, write_byte);
        if (out_read_byte != nullptr) {
            *out_read_byte = std::get<1>(ret);
        }

        return std::get<0>(ret);
    }
};

TEST(Sdio, IntrRegister) {
    fake_ddk::Bind ddk;

    wifi_config_t config = {ZX_INTERRUPT_MODE_LEVEL_LOW};
    ddk.SetMetadata(&config, sizeof(config));

    brcmf_sdio_dev dev = {};
    sdio_func func1 = {};
    MockSdio sdio;
    ddk::MockGpio gpio;
    brcmf_bus bus_if = {};
    brcmf_mp_device settings = {};

    dev.func1 = &func1;
    dev.gpios[WIFI_OOB_IRQ_GPIO_INDEX] = *gpio.GetProto();
    dev.sdio_proto = *sdio.GetProto();
    dev.bus_if = &bus_if;
    dev.settings = &settings;

    gpio.ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
        .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_LEVEL_LOW, zx::interrupt(ZX_HANDLE_INVALID));
    sdio.ExpectEnableFnIntr(ZX_OK, SDIO_FN_1)
        .ExpectEnableFnIntr(ZX_OK, SDIO_FN_2)
        .ExpectDoVendorControlRwByte(ZX_OK, true, SDIO_CCCR_BRCM_SEPINT,
                                     SDIO_CCCR_BRCM_SEPINT_MASK | SDIO_CCCR_BRCM_SEPINT_OE, 0);

    EXPECT_OK(brcmf_sdiod_intr_register(&dev));

    gpio.VerifyAndClear();
    sdio.VerifyAndClear();
}

TEST(Sdio, IntrUnregister) {
    brcmf_sdio_dev dev = {};
    sdio_func func1 = {};

    MockSdio sdio;
    dev.func1 = &func1;
    dev.sdio_proto = *sdio.GetProto();
    dev.oob_irq_requested = true;

    sdio.ExpectDoVendorControlRwByte(ZX_OK, true, 0xf2, 0, 0)
        .ExpectDisableFnIntr(ZX_OK, 1)
        .ExpectDisableFnIntr(ZX_OK, 2);

    brcmf_sdiod_intr_unregister(&dev);

    sdio.VerifyAndClear();

    dev = {};
    func1 = {};

    dev.func1 = &func1;
    dev.sdio_proto = *sdio.GetProto();
    dev.sd_irq_requested = true;

    sdio.ExpectDisableFnIntr(ZX_OK, 2).ExpectDisableFnIntr(ZX_OK, 1);

    brcmf_sdiod_intr_unregister(&dev);

    sdio.VerifyAndClear();
}

TEST(Sdio, VendorControl) {
    brcmf_sdio_dev dev = {};

    MockSdio sdio;
    dev.sdio_proto = *sdio.GetProto();

    sdio.ExpectDoVendorControlRwByte(ZX_ERR_IO, false, 0xf0, 0, 0xab)
        .ExpectDoVendorControlRwByte(ZX_OK, false, 0xf3, 0, 0x12)
        .ExpectDoVendorControlRwByte(ZX_ERR_BAD_STATE, true, 0xff, 0x55, 0)
        .ExpectDoVendorControlRwByte(ZX_ERR_TIMED_OUT, true, 0xfd, 0x79, 0);

    zx_status_t status;

    EXPECT_EQ(brcmf_sdiod_func0_rb(&dev, 0xf0, &status), 0xab);
    EXPECT_EQ(status, ZX_ERR_IO);
    EXPECT_EQ(brcmf_sdiod_func0_rb(&dev, 0xf3, nullptr), 0x12);

    brcmf_sdiod_func0_wb(&dev, 0xff, 0x55, nullptr);
    brcmf_sdiod_func0_wb(&dev, 0xfd, 0x79, &status);
    EXPECT_EQ(status, ZX_ERR_TIMED_OUT);

    sdio.VerifyAndClear();
}

}  // namespace
