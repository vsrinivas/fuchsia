// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/test.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/test.h>

namespace {

class TestDevice;
using TestDeviceType = ddk::Device<TestDevice, ddk::Ioctlable>;

class TestDevice : public TestDeviceType,
                   public ddk::TestProtocol<TestDevice> {
public:
    TestDevice(zx_device_t* parent) : TestDeviceType(parent) { }

    // Methods required by the ddk mixins
    zx_status_t DdkIoctl(uint32_t op, const void* in, size_t inlen, void* out,
                         size_t outlen, size_t* out_actual);
    void DdkRelease();

    // Methods required by the TestProtocol mixin
    void TestSetOutputSocket(zx_handle_t handle);
    zx_handle_t TestGetOutputSocket();
    void TestSetTestFunc(const test_func_t* func);
    zx_status_t TestRunTests(test_report_t* out_report);
    void TestDestroy();
private:
    zx::socket output_;
    test_func_t test_func_;
};

class TestRootDevice;
using TestRootDeviceType = ddk::Device<TestRootDevice, ddk::Ioctlable>;

class TestRootDevice : public TestRootDeviceType {
public:
    TestRootDevice(zx_device_t* parent) : TestRootDeviceType(parent) { }

    zx_status_t Bind() {
        return DdkAdd("test");
    }

    // Methods required by the ddk mixins
    zx_status_t DdkIoctl(uint32_t op, const void* in, size_t inlen, void* out,
                         size_t outlen, size_t* out_actual);
    void DdkRelease() { ZX_ASSERT_MSG(false, "TestRootDevice::DdkRelease() not supported\n"); }
};

void TestDevice::TestSetOutputSocket(zx_handle_t handle) {
    output_.reset(handle);
}

zx_handle_t TestDevice::TestGetOutputSocket() {
    return output_.get();
}

void TestDevice::TestSetTestFunc(const test_func_t* func) {
    test_func_ = *func;
}

zx_status_t TestDevice::TestRunTests(test_report_t* report) {
    if (test_func_.callback == NULL) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return test_func_.callback(test_func_.ctx, report);
}

void TestDevice::TestDestroy() {
    DdkRemove();
}

zx_status_t TestDevice::DdkIoctl(uint32_t op, const void* in, size_t inlen, void* out,
                                 size_t outlen, size_t* out_actual) {
    switch (op) {
    case IOCTL_TEST_SET_OUTPUT_SOCKET:
        if (inlen != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        TestSetOutputSocket(*(zx_handle_t*)in);
        return ZX_OK;

    case IOCTL_TEST_RUN_TESTS: {
        if (outlen != sizeof(test_report_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        *out_actual = sizeof(test_report_t);
        return TestRunTests(static_cast<test_report_t*>(out));
    }

    case IOCTL_TEST_DESTROY_DEVICE:
        TestDestroy();
        return 0;

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void TestDevice::DdkRelease() {
    delete this;
}

zx_status_t TestRootDevice::DdkIoctl(uint32_t op, const void* in, size_t inlen,
                                     void* out, size_t outlen, size_t* out_actual) {
    if (op != IOCTL_TEST_CREATE_DEVICE) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    char devname[ZX_DEVICE_NAME_MAX + 1];
    if (inlen > 0) {
        strncpy(devname, static_cast<const char*>(in), sizeof(devname) - 1);
    } else {
        strncpy(devname, "testdev", sizeof(devname) - 1);
    }
    devname[sizeof(devname) - 1] = '\0';
    // truncate trailing ".so"
    if (!strcmp(devname + strlen(devname) - 3, ".so")) {
        devname[strlen(devname) - 3] = 0;
    }

    if (outlen < strlen(devname) + sizeof(TEST_CONTROL_DEVICE) + 1) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    auto device = fbl::make_unique<TestDevice>(zxdev());
    zx_status_t status = device->DdkAdd(devname);
    if (status != ZX_OK) {
        return status;
    }
    // devmgr now owns this
    __UNUSED auto ptr = device.release();

    int length = snprintf(static_cast<char*>(out), outlen,"%s/%s", TEST_CONTROL_DEVICE, devname)
            + 1;
    *out_actual = length;
    return ZX_OK;
}

zx_status_t TestDriverBind(void* ctx, zx_device_t* dev) {
    auto root = fbl::make_unique<TestRootDevice>(dev);
    zx_status_t status = root->Bind();
    if (status != ZX_OK) {
        return status;
    }
    // devmgr now owns root
    __UNUSED auto ptr = root.release();
    return ZX_OK;
}

const zx_driver_ops_t kTestDriverOps = []() {
    zx_driver_ops_t driver;
    driver.version = DRIVER_OPS_VERSION;
    driver.bind = TestDriverBind;
    return driver;
}();

} // namespace

ZIRCON_DRIVER_BEGIN(test, kTestDriverOps, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(test)
