// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>

#define DRIVER_NAME "test-clock"

namespace clock {

class TestClockDevice;
using DeviceType = ddk::Device<TestClockDevice, ddk::Unbindable>;

class TestClockDevice : public DeviceType,
                        public ddk::ClockImplProtocol<TestClockDevice, ddk::base_protocol> {
public:
    static zx_status_t Create(zx_device_t* parent);

    explicit TestClockDevice(zx_device_t* parent)
        : DeviceType(parent) {}

    zx_status_t Create(std::unique_ptr<TestClockDevice>* out);
    zx_status_t Init();

    // Methods required by the ddk mixins
    void DdkUnbind();
    void DdkRelease();

    zx_status_t ClockImplEnable(uint32_t clock_id);
    zx_status_t ClockImplDisable(uint32_t clock_id);

private:
    static constexpr uint32_t MIN_CLOCK = 2;
    static constexpr uint32_t MAX_CLOCK = 8;
};

zx_status_t TestClockDevice::Init() {
    pbus_protocol_t pbus;
    auto status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d\n", __func__, status);
        return status;
    }
    clock_impl_protocol_t clock_proto = {
        .ops = &clock_impl_protocol_ops_,
        .ctx = this,
    };
    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_CLOCK_IMPL, &clock_proto,
                                    sizeof(clock_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_register_protocol failed %d\n", __func__, status);
        return status;
    }
    return ZX_OK;
}

zx_status_t TestClockDevice::Create(zx_device_t* parent) {
    auto dev = std::make_unique<TestClockDevice>(parent);
    pdev_protocol_t pdev;
    zx_status_t status;

    zxlogf(INFO, "TestClockDevice::Create: %s", DRIVER_NAME);

    status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_PDEV\n", __func__);
        return status;
    }

    status = dev->DdkAdd("test-clock");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed: %d\n", __func__, status);
        return status;
    }
    // devmgr is now in charge of dev.
    auto ptr = dev.release();

    return ptr->Init();
}

void TestClockDevice::DdkUnbind() {}

void TestClockDevice::DdkRelease() {
    delete this;
}

zx_status_t TestClockDevice::ClockImplEnable(uint32_t clock_id) {
    if (clock_id < MIN_CLOCK || clock_id > MAX_CLOCK) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplDisable(uint32_t clock_id) {
    if (clock_id < MIN_CLOCK || clock_id > MAX_CLOCK) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

zx_status_t test_clock_bind(void* ctx, zx_device_t* parent) {
    return TestClockDevice::Create(parent);
}

constexpr zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t driver_ops = {};
    driver_ops.version = DRIVER_OPS_VERSION;
    driver_ops.bind = test_clock_bind;
    return driver_ops;
}();

} // namespace clock

ZIRCON_DRIVER_BEGIN(test_clock, clock::driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_CLOCK),
    ZIRCON_DRIVER_END(test_clock)
