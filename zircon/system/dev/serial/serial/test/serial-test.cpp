// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <memory>
#include <unittest/unittest.h>

namespace {

constexpr size_t kBufferLength = 16;

// Fake for the SerialImpl protocol.
class FakeSerialImpl : public ddk::SerialImplProtocol<FakeSerialImpl> {
public:
    FakeSerialImpl()
        : proto_({&serial_impl_protocol_ops_, this}) {}

    const serial_impl_protocol_t* proto() const { return &proto_; }

    bool enabled() { return enabled_; }
    const serial_notify_t* callback() { return callback_; }

    char* read_buffer() { return read_buffer_; }
    char* write_buffer() { return write_buffer_; }

    // Raw nand protocol:
    zx_status_t SerialImplGetInfo(serial_port_info_t* info) {
        return ZX_OK;
    }

    zx_status_t SerialImplConfig(uint32_t baud_rate, uint32_t flags) {
        return ZX_OK;
    }

    zx_status_t SerialImplEnable(bool enable) {
        enabled_ = enable;
        return ZX_OK;
    }

    zx_status_t SerialImplRead(void* buf, size_t length, size_t* out_actual) {
        char* buffer = static_cast<char*>(buf);
        size_t i;

        for (i = 0; i < length && i < kBufferLength && read_buffer_[i]; ++i) {
            buffer[i] = read_buffer_[i];
        }
        *out_actual = i;

        return ZX_OK;
    }

    zx_status_t SerialImplWrite(const void* buf, size_t length, size_t* out_actual) {
        const char* buffer = static_cast<const char*>(buf);
        size_t i;

        for (i = 0; i < length && i < kBufferLength && buffer[i]; ++i) {
            write_buffer_[i] = buffer[i];
        }
        *out_actual = i;

        return ZX_OK;
    }

    zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb) {
        callback_ = cb;
        return ZX_OK;
    }

private:
    serial_impl_protocol_t proto_;
    bool enabled_;
    const serial_notify_t* callback_;
    char read_buffer_[kBufferLength];
    char write_buffer_[kBufferLength];
};

class SerialTester {
public:
    SerialTester() {
        fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
        protocols[0] = {ZX_PROTOCOL_SERIAL_IMPL,
                        *reinterpret_cast<const fake_ddk::Protocol*>(serial_impl_.proto())};
        ddk_.SetProtocols(std::move(protocols));
    }

    fake_ddk::Bind& ddk() { return ddk_; }
    FakeSerialImpl& serial_impl() { return serial_impl_; }

private:
   fake_ddk::Bind ddk_;
   FakeSerialImpl serial_impl_;
};

// Provides control primitives for tests that issue IO requests to the device.
class SerialDeviceTest {
public:
    SerialDeviceTest();
    ~SerialDeviceTest() {}

    serial::SerialDevice* device() { return device_.get(); }
    FakeSerialImpl& serial_impl() { return tester_.serial_impl(); }

    DISALLOW_COPY_ASSIGN_AND_MOVE(SerialDeviceTest);

private:
    SerialTester tester_;
    std::unique_ptr<serial::SerialDevice> device_;
};

SerialDeviceTest::SerialDeviceTest() {
    device_ = std::make_unique<serial::SerialDevice>(fake_ddk::kFakeParent);

    if (ZX_OK != device_->Init()) {
        device_.reset();
    }
}

bool SerialInitNoParentProtocolTest() {
    BEGIN_TEST;
    // SerialTester is intentionally not defined in this scope as it would
    // define the ZX_PROTOCOL_SERIAL_IMPL protocol.
    serial::SerialDevice device(fake_ddk::kFakeParent);
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, device.Init());
    END_TEST;
}

bool SerialInitTest() {
    BEGIN_TEST;
    SerialTester tester;
    serial::SerialDevice device(fake_ddk::kFakeParent);
    ASSERT_EQ(ZX_OK, device.Init());
    END_TEST;
}

bool DdkLifetimeTest() {
    BEGIN_TEST;
    SerialTester tester;
    serial::SerialDevice* device(new serial::SerialDevice(fake_ddk::kFakeParent));

    ASSERT_EQ(ZX_OK, device->Init());
    ASSERT_EQ(ZX_OK, device->Bind());
    device->DdkRemove();
    EXPECT_TRUE(tester.ddk().Ok());

    // Delete the object.
    device->DdkRelease();
    END_TEST;
}

bool DdkReleaseTest() {
    BEGIN_TEST;

    SerialTester tester;
    serial::SerialDevice* device(new serial::SerialDevice(fake_ddk::kFakeParent));
    FakeSerialImpl& serial_impl = tester.serial_impl();

    ASSERT_EQ(ZX_OK, device->Init());

    // Manually set enabled to true.
    serial_impl.SerialImplEnable(true);
    EXPECT_TRUE(serial_impl.enabled());

    device->DdkRelease();

    EXPECT_FALSE(serial_impl.enabled());
    ASSERT_EQ(nullptr, serial_impl.callback()->callback);

    END_TEST;
}

bool DdkOpenTest() {
    BEGIN_TEST;

    SerialDeviceTest test;
    serial::SerialDevice* device = test.device();
    FakeSerialImpl& serial_impl = test.serial_impl();

    ASSERT_EQ(ZX_OK, device->DdkOpen(nullptr /* dev_out */, 0 /* flags */));

    EXPECT_TRUE(serial_impl.enabled());
    // Callback is not null.
    ASSERT_TRUE(serial_impl.callback()->callback);

    // Verify state.
    ASSERT_EQ(ZX_ERR_ALREADY_BOUND, device->DdkOpen(nullptr /* dev_out */, 0 /* flags */));

    END_TEST;
}

bool DdkCloseTest() {
    BEGIN_TEST;

    SerialDeviceTest test;
    serial::SerialDevice* device = test.device();
    FakeSerialImpl& serial_impl = test.serial_impl();

    ASSERT_EQ(ZX_OK, device->DdkOpen(nullptr /* dev_out */, 0 /* flags */));
    ASSERT_EQ(ZX_OK, device->DdkClose(0 /* flags */));

    EXPECT_FALSE(serial_impl.enabled());
    ASSERT_EQ(nullptr, serial_impl.callback()->callback);

    // Verify state.
    ASSERT_EQ(ZX_ERR_BAD_STATE, device->DdkClose(0 /* flags */));

    END_TEST;
}

bool DdkReadTest() {
    BEGIN_TEST;

    SerialDeviceTest test;
    serial::SerialDevice* device = test.device();
    FakeSerialImpl& serial_impl = test.serial_impl();

    const char* expected = "test";
    char buffer[kBufferLength];
    size_t read_len;

    // Try to read without opening.
    ASSERT_EQ(ZX_ERR_BAD_STATE, device->DdkRead(buffer, kBufferLength, 0, &read_len));

    // Test set up.
    strcpy(serial_impl.read_buffer(), expected);
    ASSERT_EQ(ZX_OK, device->DdkOpen(nullptr /* dev_out */, 0 /* flags */));

    // Test.
    ASSERT_EQ(ZX_OK, device->DdkRead(buffer, kBufferLength, 0, &read_len));
    ASSERT_EQ(4, read_len);
    ASSERT_EQ(0, strncmp(expected, buffer, read_len));

    END_TEST;
}

bool DdkWriteTest() {
    BEGIN_TEST;

    SerialDeviceTest test;
    serial::SerialDevice* device = test.device();
    FakeSerialImpl& serial_impl = test.serial_impl();

    const char* data = "test";
    char buffer[kBufferLength];
    size_t write_len;

    // Try to write without opening.
    ASSERT_EQ(ZX_ERR_BAD_STATE, device->DdkWrite(buffer, kBufferLength, 0, &write_len));

    // Test set up.
    ASSERT_EQ(ZX_OK, device->DdkOpen(nullptr /* dev_out */, 0 /* flags */));

    // Test.
    ASSERT_EQ(ZX_OK, device->DdkWrite(data, kBufferLength, 0, &write_len));
    ASSERT_EQ(4, write_len);
    ASSERT_EQ(0, strncmp(data, serial_impl.write_buffer(), write_len));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(SerialDeviceTests)
RUN_TEST_SMALL(SerialInitNoParentProtocolTest)
RUN_TEST_SMALL(SerialInitTest)
RUN_TEST_SMALL(DdkLifetimeTest)
RUN_TEST_SMALL(DdkReleaseTest)
RUN_TEST_SMALL(DdkOpenTest)
RUN_TEST_SMALL(DdkCloseTest)
RUN_TEST_SMALL(DdkReadTest)
RUN_TEST_SMALL(DdkWriteTest)
END_TEST_CASE(SerialDeviceTests)
