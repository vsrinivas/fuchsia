// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167.h"

#include <zxtest/zxtest.h>

namespace board_mt8167 {

class Mt8167Test : public Mt8167, public ddk::PBusProtocol<Mt8167Test> {
public:
    Mt8167Test() : Mt8167(nullptr) {
        pbus_protocol_t pbus_proto = {.ops = &pbus_protocol_ops_, .ctx = this};
        pbus_ = ddk::PBusProtocolClient(&pbus_proto);
    }

    bool Ok() { return vgp1_enable_called_ && thermal_enable_called_second_; }

    // These stubs ensure the power device setup succeeds.
    zx_status_t PBusDeviceAdd(const pbus_dev_t* dev) { return ZX_OK; }
    zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) { return ZX_OK; }
    zx_status_t PBusRegisterProtocol(uint32_t proto_id, const void* protocol_buffer,
                                     size_t protocol_size) {
        return ZX_OK;
    }
    zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info) { return ZX_OK; }
    zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info) { return ZX_OK; }
    zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cb) {
        return ZX_OK;
    }
    zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev,
                                       const device_component_t* components_list,
                                       size_t components_count,
                                       uint32_t t_coresident_device_index) {
        return ZX_OK;
    }

private:
    zx_status_t Vgp1Enable() override {
        vgp1_enable_called_ = true;
        return ZX_OK;
    }

    zx_status_t Msdc0Init() override { return ZX_OK; }
    zx_status_t Msdc2Init() override { return ZX_OK; }
    zx_status_t SocInit() override { return ZX_OK; }
    zx_status_t SysmemInit() override { return ZX_OK; }
    zx_status_t GpioInit() override { return ZX_OK; }
    zx_status_t GpuInit() override { return ZX_OK; }
    zx_status_t DisplayInit() override { return ZX_OK; }
    zx_status_t I2cInit() override { return ZX_OK; }
    zx_status_t ButtonsInit() override { return ZX_OK; }
    zx_status_t ClkInit() override { return ZX_OK; }
    zx_status_t UsbInit() override { return ZX_OK; }
    zx_status_t ThermalInit() override {
        thermal_enable_called_second_ = vgp1_enable_called_;
        return ZX_OK;
    }
    zx_status_t TouchInit() override { return ZX_OK; }
    zx_status_t BacklightInit() override { return ZX_OK; }
    zx_status_t AudioInit() override { return ZX_OK; }

    bool vgp1_enable_called_ = false;
    bool thermal_enable_called_second_ = false;
};

TEST(Mt8167Test, PmicInitOrder) {
    Mt8167Test dut;
    EXPECT_EQ(0, dut.Thread());
    EXPECT_TRUE(dut.Ok());
}

} // namespace board_mt8167
