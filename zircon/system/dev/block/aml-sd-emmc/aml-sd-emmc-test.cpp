// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-sd-emmc.h"

#include <fbl/auto_call.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <zxtest/zxtest.h>

namespace {
static constexpr uint32_t kMmioRegCount = 768;
} // namespace

namespace sdmmc {

class AmlSdEmmcTest : public AmlSdEmmc {
public:
    AmlSdEmmcTest(mmio_buffer_t& mmio, mmio_pinned_buffer_t& pinned_mmio)
        : AmlSdEmmc(fake_ddk::kFakeParent,
                    zx::bti(ZX_HANDLE_INVALID),
                    ddk::MmioBuffer(mmio),
                    ddk::MmioPinnedBuffer(pinned_mmio),
                    aml_sd_emmc_config_t {
                        .supports_dma = false,
                        .min_freq = 400000,
                        .max_freq = 120000000,
                    },
                    zx::interrupt(ZX_HANDLE_INVALID),
                    ddk::GpioProtocolClient()) {}
    zx_status_t TestDdkAdd() {
        // call parent's bind
        return Bind();
    }
};

TEST(AmlSdEmmcTest, DdkLifecycle) {
    ddk_mock::MockMmioReg reg_array[kMmioRegCount];
    ddk_mock::MockMmioRegRegion regs(reg_array, sizeof(uint32_t), kMmioRegCount);
    mmio_buffer_t buff = regs.GetMmioBuffer();
    mmio_pinned_buffer_t pinned_mmio = {
        .mmio = &buff,
        .pmt = ZX_HANDLE_INVALID,
        .paddr = 0x100,
    };
    AmlSdEmmcTest dut(buff, pinned_mmio);
    fake_ddk::Bind ddk;
    EXPECT_OK(dut.TestDdkAdd());
    dut.DdkUnbind();
    EXPECT_TRUE(ddk.Ok());
}

} //sdmmc
