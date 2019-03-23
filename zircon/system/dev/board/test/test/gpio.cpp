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
#include <ddktl/protocol/gpioimpl.h>

#define DRIVER_NAME "test-gpio"

namespace gpio {

class TestGpioDevice;
using DeviceType = ddk::Device<TestGpioDevice, ddk::Unbindable>;

class TestGpioDevice : public DeviceType,
                       public ddk::GpioImplProtocol<TestGpioDevice, ddk::base_protocol> {
public:
    static zx_status_t Create(zx_device_t* parent);

    explicit TestGpioDevice(zx_device_t* parent)
        : DeviceType(parent) {}

    zx_status_t Create(std::unique_ptr<TestGpioDevice>* out);
    zx_status_t Init();

    // Methods required by the ddk mixins
    void DdkUnbind();
    void DdkRelease();

    zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
    zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
    zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
    zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
    zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
    zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
    zx_status_t GpioImplReleaseInterrupt(uint32_t index);
    zx_status_t GpioImplSetPolarity(uint32_t index, uint32_t polarity);
    zx_status_t GpioImplSetDriveStrength(uint32_t index, uint8_t mA) {
        return ZX_ERR_NOT_SUPPORTED;
    }
};

zx_status_t TestGpioDevice::Init() {
    pbus_protocol_t pbus;
    auto status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d\n", __func__, status);
        return status;
    }
    gpio_impl_protocol_t gpio_proto = {
        .ops = &gpio_impl_protocol_ops_,
        .ctx = this,
    };
    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_register_protocol failed %d\n", __func__, status);
        return status;
    }
    return ZX_OK;
}

zx_status_t TestGpioDevice::Create(zx_device_t* parent) {
    auto dev = std::make_unique<TestGpioDevice>(parent);
    pdev_protocol_t pdev;
    zx_status_t status;

    zxlogf(INFO, "TestGpioDevice::Create: %s \n", DRIVER_NAME);

    status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_PDEV\n", __func__);
        return status;
    }

    status = dev->DdkAdd("test-gpio");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed: %d\n", __func__, status);
        return status;
    }
    // devmgr is now in charge of dev.
    auto ptr = dev.release();

    return ptr->Init();
}

void TestGpioDevice::DdkUnbind() {}

void TestGpioDevice::DdkRelease() {
    delete this;
}

zx_status_t TestGpioDevice::GpioImplConfigIn(uint32_t index, uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TestGpioDevice::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TestGpioDevice::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TestGpioDevice::GpioImplRead(uint32_t index, uint8_t* out_value) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TestGpioDevice::GpioImplWrite(uint32_t index, uint8_t value) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TestGpioDevice::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                                 zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TestGpioDevice::GpioImplReleaseInterrupt(uint32_t index) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TestGpioDevice::GpioImplSetPolarity(uint32_t index, uint32_t polarity) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t test_gpio_bind(void* ctx, zx_device_t* parent) {
    return TestGpioDevice::Create(parent);
}

constexpr zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t driver_ops = {};
    driver_ops.version = DRIVER_OPS_VERSION;
    driver_ops.bind = test_gpio_bind;
    return driver_ops;
}();

} // namespace gpio

ZIRCON_DRIVER_BEGIN(test_gpio, gpio::driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_GPIO),
    ZIRCON_DRIVER_END(test_gpio)
