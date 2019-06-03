// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial.h"

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <memory>
#include <unittest/unittest.h>
#include <zircon/thread_annotations.h>

namespace {

constexpr size_t kBufferLength = 16;

constexpr zx_signals_t kEventWrittenSignal = ZX_USER_SIGNAL_0;

// Fake for the SerialImpl protocol.
class FakeSerialImpl : public ddk::SerialImplProtocol<FakeSerialImpl> {
public:
    FakeSerialImpl()
        : proto_({&serial_impl_protocol_ops_, this}) {
        zx::event::create(0, &write_event_);
    }

    // Getters.
    const serial_impl_protocol_t* proto() const { return &proto_; }
    bool enabled() { return enabled_; }

    const serial_notify_t* callback() {
        fbl::AutoLock al(&cb_lock_);
        return &callback_;
    }

    char* read_buffer() { return read_buffer_; }
    const char* write_buffer() { return write_buffer_; }
    size_t write_buffer_length() { return write_buffer_length_; }

    // Test utility methods.
    void set_state_and_notify(serial_state_t state) {
        fbl::AutoLock al(&cb_lock_);

        state_ = state;
        if (callback_.callback) {
            callback_.callback(callback_.ctx, state_);
        }
    }

    zx_status_t wait_for_write(zx::time deadline, zx_signals_t* pending) {
        return write_event_.wait_one(kEventWrittenSignal, deadline, pending);
    }

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
        if (!(state_ & SERIAL_STATE_READABLE)) {
            *out_actual = 0;
            return ZX_ERR_SHOULD_WAIT;
        }

        char* buffer = static_cast<char*>(buf);
        size_t i;

        for (i = 0; i < length && i < kBufferLength && read_buffer_[i]; ++i) {
            buffer[i] = read_buffer_[i];
        }
        *out_actual = i;

        if (i == kBufferLength || !read_buffer_[i]) {
            // Simply reset the state, no advanced state machine.
            set_state_and_notify(0);
        }

        return ZX_OK;
    }

    zx_status_t SerialImplWrite(const void* buf, size_t length, size_t* out_actual) {
        const char* buffer = static_cast<const char*>(buf);
        size_t i;

        for (i = 0; i < length && i < kBufferLength && buffer[i]; ++i) {
            write_buffer_[i] = buffer[i];
        }
        *out_actual = i;

        // Signal that the write_buffer has been written to.
        if (i > 0) {
            write_buffer_length_ = i;
            write_event_.signal(0, kEventWrittenSignal);
        }

        return ZX_OK;
    }

    zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb) {
        fbl::AutoLock al(&cb_lock_);
        callback_.callback = cb->callback;
        callback_.ctx = cb->ctx;
        return ZX_OK;
    }

private:
    serial_impl_protocol_t proto_;
    bool enabled_;

    fbl::Mutex cb_lock_;
    serial_notify_t callback_ TA_GUARDED(cb_lock_) = {nullptr, nullptr};
    serial_state_t state_;

    char read_buffer_[kBufferLength];
    char write_buffer_[kBufferLength];
    size_t write_buffer_length_;

    zx::event write_event_;
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
    ~SerialDeviceTest();

    serial::SerialDevice* device() { return device_; }
    FakeSerialImpl& serial_impl() { return tester_.serial_impl(); }

    DISALLOW_COPY_ASSIGN_AND_MOVE(SerialDeviceTest);

private:
    SerialTester tester_;
    serial::SerialDevice* device_;
};

SerialDeviceTest::SerialDeviceTest() {
    device_ = new serial::SerialDevice(fake_ddk::kFakeParent);

    if (ZX_OK != device_->Init()) {
        delete device_;
        device_ = nullptr;
    }
}

SerialDeviceTest::~SerialDeviceTest() {
    device_->DdkRelease();
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
    serial_impl.set_state_and_notify(SERIAL_STATE_READABLE);
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

bool SerialOpenSocketTest() {
    BEGIN_TEST;

    SerialDeviceTest test;
    serial::SerialDevice* device = test.device();
    FakeSerialImpl& serial_impl = test.serial_impl();
    zx::socket socket;

    const char* data = "test";
    char buffer[kBufferLength];
    size_t length;

    ASSERT_EQ(ZX_OK, device->SerialOpenSocket(&socket));
    // Trivial state check.
    ASSERT_EQ(ZX_ERR_ALREADY_BOUND, device->SerialOpenSocket(&socket));

    ////////////////////
    // Serial -> Socket.
    strcpy(serial_impl.read_buffer(), data);
    // Notify device that serial is readable.
    serial_impl.set_state_and_notify(SERIAL_STATE_READABLE);

    zx_signals_t pending;
    // Wait and read from socket.
    ASSERT_EQ(ZX_OK, socket.wait_one(ZX_SOCKET_READABLE, zx::time::infinite(), &pending));
    ASSERT_TRUE(pending & ZX_SOCKET_READABLE);
    ASSERT_EQ(ZX_OK, socket.read(0, buffer, kBufferLength, &length));
    ASSERT_EQ(4, length);
    ASSERT_EQ(0, strncmp(data, buffer, length));

    ////////////////////
    // Socket -> Serial.
    ASSERT_EQ(ZX_OK, socket.write(0, data, 4, &length));
    ASSERT_EQ(4, length);

    //Notify device that serial is writable.
    serial_impl.set_state_and_notify(SERIAL_STATE_WRITABLE);
    // Wait and read from device.
    ASSERT_EQ(ZX_OK, serial_impl.wait_for_write(zx::time::infinite(), &pending));
    ASSERT_TRUE(pending & kEventWrittenSignal);
    ASSERT_EQ(4, serial_impl.write_buffer_length());
    ASSERT_EQ(0, strncmp(data, serial_impl.write_buffer(), 4));

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
RUN_TEST_SMALL(SerialOpenSocketTest)
END_TEST_CASE(SerialDeviceTests)
