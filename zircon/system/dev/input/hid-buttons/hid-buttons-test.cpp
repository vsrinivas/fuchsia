// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-buttons.h"

#include <ddk/metadata.h>
#include <ddk/metadata/buttons.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/protocol/gpio.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-gpio/mock-gpio.h>
#include <zxtest/zxtest.h>

namespace {
static const buttons_button_config_t buttons_direct[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
};

static const buttons_gpio_config_t gpios_direct[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}}};

static const buttons_button_config_t buttons_matrix[] = {
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_VOLUME_UP, 0, 2, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_A, 1, 2, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_M, 0, 3, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_PLAY_PAUSE, 1, 3, 0},
};

static const buttons_gpio_config_t gpios_matrix[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_PULL_UP}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_PULL_UP}},
    {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, 0, {0}},
    {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, 0, {0}},
};
} // namespace

namespace buttons {

enum class TestType {
    kTestDirect,
    kTestMatrix,
};

class FakeDdkButtons : public fake_ddk::Bind {
public:
    FakeDdkButtons(TestType type)
        : Bind(), type_(type) {}

    zx_status_t DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* buf, size_t buflen,
                                  size_t* actual) {
        switch (type) {
        case DEVICE_METADATA_BUTTONS_BUTTONS:
            switch (type_) {
            case TestType::kTestDirect:
                *actual = sizeof(buttons_direct);
                if (buflen != sizeof(buttons_direct)) {
                    return ZX_ERR_INTERNAL;
                }
                memcpy(buf, &buttons_direct[0], *actual);
                break;
            case TestType::kTestMatrix:
                *actual = sizeof(buttons_matrix);
                if (buflen != sizeof(buttons_matrix)) {
                    return ZX_ERR_INTERNAL;
                }
                memcpy(buf, &buttons_matrix[0], *actual);
                break;
            }
            break;
        case DEVICE_METADATA_BUTTONS_GPIOS:
            switch (type_) {
            case TestType::kTestDirect:
                *actual = sizeof(gpios_direct);
                if (buflen != sizeof(gpios_direct)) {
                    return ZX_ERR_INTERNAL;
                }
                memcpy(buf, &gpios_direct[0], *actual);
                break;
            case TestType::kTestMatrix:
                *actual = sizeof(gpios_matrix);
                if (buflen != sizeof(gpios_matrix)) {
                    return ZX_ERR_INTERNAL;
                }
                memcpy(buf, &gpios_matrix[0], *actual);
                break;
            }
            break;
        default:
            return ZX_ERR_INTERNAL;
        }
        return ZX_OK;
    }

    zx_status_t DeviceGetMetadataSize(zx_device_t* dev, uint32_t type, size_t* out_size) {
        switch (type) {
        case DEVICE_METADATA_BUTTONS_BUTTONS:
            switch (type_) {
            case TestType::kTestDirect:
                *out_size = sizeof(buttons_direct);
                break;
            case TestType::kTestMatrix:
                *out_size = sizeof(buttons_matrix);
                break;
            }
            break;
        case DEVICE_METADATA_BUTTONS_GPIOS:
            switch (type_) {
            case TestType::kTestDirect:
                *out_size = sizeof(gpios_direct);
                break;
            case TestType::kTestMatrix:
                *out_size = sizeof(gpios_matrix);
                break;
            }
            break;
        default:
            return ZX_ERR_INTERNAL;
        }
        return ZX_OK;
    }

private:
    TestType type_;
};

class HidButtonsDeviceTest : public HidButtonsDevice {
public:
    HidButtonsDeviceTest(mock_gpio::MockGpio* gpios, size_t gpios_count, TestType type)
        : HidButtonsDevice(fake_ddk::kFakeParent), type_(type) {
        fbl::AllocChecker ac;
        using MockPointer = mock_gpio::MockGpio*;
        gpio_mocks_.reset(new (&ac) MockPointer[gpios_count], gpios_count);
        for (size_t i = 0; i < gpios_count; ++i) {
            gpio_mocks_[i] = &gpios[i];
        }
    }
    void ShutDownTest() {
        HidButtonsDevice::ShutDown();
    }

    void SetupGpio(const zx::interrupt& irq, size_t gpio_index) {
        auto* mock = gpio_mocks_[gpio_index];
        mock->ExpectSetAltFunction(ZX_OK, 0);
        switch (type_) {
        case TestType::kTestDirect:
            mock->ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
                .ExpectRead(ZX_OK, 0) // Not pushed, low.
                .ExpectReleaseInterrupt(ZX_OK)
                .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_HIGH, irq);

            // Make sure polarity is correct in case it changed during configuration.
            mock->ExpectRead(ZX_OK, 0)                        // Not pushed.
                .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH) // Set correct polarity.
                .ExpectRead(ZX_OK, 0);                        // Still not pushed.
            break;
        case TestType::kTestMatrix:
            if (gpios_matrix[gpio_index].type == BUTTONS_GPIO_TYPE_INTERRUPT) {
                mock->ExpectConfigIn(ZX_OK, gpios_matrix[gpio_index].internal_pull)
                    .ExpectRead(ZX_OK, 0) // Not pushed, low.
                    .ExpectReleaseInterrupt(ZX_OK)
                    .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_HIGH, irq);

                // Make sure polarity is correct in case it changed during configuration.
                mock->ExpectRead(ZX_OK, 0)                        // Not pushed.
                    .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH) // Set correct polarity.
                    .ExpectRead(ZX_OK, 0);                        // Still not pushed.
            } else {
                mock->ExpectConfigOut(ZX_OK, gpios_matrix[gpio_index].output_value);
            }
            break;
        }
    }

    zx_status_t BindTest() {
        FakeDdkButtons ddk(type_); // In these tests this is only used during binding.
        fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
        protocols[0] = {ZX_PROTOCOL_PDEV, {nullptr, nullptr}};
        ddk.SetProtocols(std::move(protocols));
        return HidButtonsDevice::Bind();
    }

    zx_status_t PdevGetGpioProtocol(const pdev_protocol_t* proto, uint32_t index,
                                    void* out_protocol_buffer, size_t out_protocol_size,
                                    size_t* out_protocol_actual) override {
        if (index > gpio_mocks_.size()) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (out_protocol_size != sizeof(gpio_protocol_t)) {
            return ZX_ERR_INTERNAL;
        }
        gpio_protocol_t* p = reinterpret_cast<gpio_protocol_t*>(out_protocol_buffer);
        *p = *gpio_mocks_[index]->GetProto();
        *out_protocol_actual = sizeof(gpio_protocol_t);
        return ZX_OK;
    }

    void FakeInterrupt() {
        // Issue the first interrupt.
        zx_port_packet packet = {kPortKeyInterruptStart + 0, ZX_PKT_TYPE_USER, ZX_OK, {}};
        zx_status_t status = port_.queue(&packet);
        ZX_ASSERT(status == ZX_OK);
    }

private:
    TestType type_;
    fbl::Array<mock_gpio::MockGpio*> gpio_mocks_;
};

TEST(HidButtonsTest, DirectButtonBind) {
    mock_gpio::MockGpio mock_gpios[1];
    HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
    zx::interrupt irq;
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
    device.SetupGpio(irq, 0);

    EXPECT_EQ(ZX_OK, device.BindTest());
    device.ShutDownTest();
    ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonPush) {
    mock_gpio::MockGpio mock_gpios[1];
    HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
    zx::interrupt irq;
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
    device.SetupGpio(irq, 0);

    // Reconfigure Polarity due to interrupt.
    mock_gpios[0].ExpectRead(ZX_OK, 1)               // Pushed.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW) // Turn the polarity.
        .ExpectRead(ZX_OK, 1);                       // Still pushed, ok to continue.
    mock_gpios[0].ExpectRead(ZX_OK, 1);              // Read value to prepare report.

    EXPECT_EQ(ZX_OK, device.BindTest());
    device.FakeInterrupt();
    device.ShutDownTest();
    ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonUnpushedReport) {
    mock_gpio::MockGpio mock_gpios[1];
    HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
    zx::interrupt irq;
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
    device.SetupGpio(irq, 0);

    // Reconfigure Polarity due to interrupt.
    mock_gpios[0].ExpectRead(ZX_OK, 0)                // Not Pushed.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH) // Keep the correct polarity.
        .ExpectRead(ZX_OK, 0);                        // Still not pushed, ok to continue.
    mock_gpios[0].ExpectRead(ZX_OK, 0);               // Read value to prepare report.

    EXPECT_EQ(ZX_OK, device.BindTest());
    hidbus_ifc_protocol_ops_t ops = {};
    ops.io_queue = [](void* ctx, const void* buffer, size_t size) {
        buttons_input_rpt_t report_volume_up = {};
        report_volume_up.rpt_id = 1;
        report_volume_up.volume_up = 0; // Unpushed.
        ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
        EXPECT_EQ(size, sizeof(report_volume_up));
    };
    hidbus_ifc_protocol_t protocol = {};
    protocol.ops = &ops;
    device.HidbusStart(&protocol);
    device.FakeInterrupt();
    device.ShutDownTest();
    ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonPushedReport) {
    mock_gpio::MockGpio mock_gpios[1];
    HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
    zx::interrupt irq;
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
    device.SetupGpio(irq, 0);

    // Reconfigure Polarity due to interrupt.
    mock_gpios[0].ExpectRead(ZX_OK, 1)               // Pushed.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW) // Turn the polarity.
        .ExpectRead(ZX_OK, 1);                       // Still pushed, ok to continue.
    mock_gpios[0].ExpectRead(ZX_OK, 1);              // Read value to prepare report.

    EXPECT_EQ(ZX_OK, device.BindTest());
    hidbus_ifc_protocol_ops_t ops = {};
    ops.io_queue = [](void* ctx, const void* buffer, size_t size) {
        buttons_input_rpt_t report_volume_up = {};
        report_volume_up.rpt_id = 1;
        report_volume_up.volume_up = 1; // Pushed
        ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
        EXPECT_EQ(size, sizeof(report_volume_up));
    };
    hidbus_ifc_protocol_t protocol = {};
    protocol.ops = &ops;
    device.HidbusStart(&protocol);
    device.FakeInterrupt();
    device.ShutDownTest();
    ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonPushUnpushPush) {
    mock_gpio::MockGpio mock_gpios[1];
    HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
    zx::interrupt irq;
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
    device.SetupGpio(irq, 0);

    // Reconfigure Polarity due to interrupt.
    mock_gpios[0].ExpectRead(ZX_OK, 1)               // Pushed.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW) // Turn the polarity.
        .ExpectRead(ZX_OK, 1);                       // Still pushed, ok to continue.
    mock_gpios[0].ExpectRead(ZX_OK, 1);              // Read value to prepare report.

    // Reconfigure Polarity due to interrupt.
    mock_gpios[0].ExpectRead(ZX_OK, 0)                // Not pushed.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH) // Turn the polarity.
        .ExpectRead(ZX_OK, 0);                        // Still not pushed, ok to continue.
    mock_gpios[0].ExpectRead(ZX_OK, 0);               // Read value to prepare report.

    // Reconfigure Polarity due to interrupt.
    mock_gpios[0].ExpectRead(ZX_OK, 1)               // Pushed.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW) // Turn the polarity.
        .ExpectRead(ZX_OK, 1);                       // Still pushed, ok to continue.
    mock_gpios[0].ExpectRead(ZX_OK, 1);              // Read value to prepare report.

    EXPECT_EQ(ZX_OK, device.BindTest());
    device.FakeInterrupt();
    device.FakeInterrupt();
    device.FakeInterrupt();
    device.ShutDownTest();
    ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonFlaky) {
    mock_gpio::MockGpio mock_gpios[1];
    HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
    zx::interrupt irq;
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
    device.SetupGpio(irq, 0);

    // Reconfigure Polarity due to interrupt and keep checking until correct.
    mock_gpios[0].ExpectRead(ZX_OK, 1)                // Pushed.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
        .ExpectRead(ZX_OK, 0)                         // Oops now not pushed! not ok, retry.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH) // Turn the polarity.
        .ExpectRead(ZX_OK, 1)                         // Oops pushed! not ok, retry.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
        .ExpectRead(ZX_OK, 0)                         // Oops now not pushed! not ok, retry.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH) // Turn the polarity.
        .ExpectRead(ZX_OK, 1)                         // Oops pushed again! not ok, retry.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
        .ExpectRead(ZX_OK, 1);                        // Now pushed and polarity set low, ok.
    // Read value to generate report.
    mock_gpios[0].ExpectRead(ZX_OK, 1); // Pushed.

    EXPECT_EQ(ZX_OK, device.BindTest());
    device.FakeInterrupt();
    device.ShutDownTest();
    ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, MatrixButtonBind) {
    mock_gpio::MockGpio mock_gpios[countof(gpios_matrix)];
    HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestMatrix);
    zx::interrupt irqs[countof(gpios_matrix)];
    for (size_t i = 0; i < countof(gpios_matrix); ++i) {
        zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
        device.SetupGpio(irqs[i], i);
    }

    EXPECT_EQ(ZX_OK, device.BindTest());
    device.ShutDownTest();
    for (size_t i = 0; i < countof(gpios_matrix); ++i) {
        ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
    }
}

TEST(HidButtonsTest, MatrixButtonPush) {
    mock_gpio::MockGpio mock_gpios[countof(gpios_matrix)];
    HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestMatrix);
    zx::interrupt irqs[countof(gpios_matrix)];
    for (size_t i = 0; i < countof(gpios_matrix); ++i) {
        zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
        device.SetupGpio(irqs[i], i);
    }

    EXPECT_EQ(ZX_OK, device.BindTest());

    // Reconfigure Polarity due to interrupt.
    mock_gpios[0].ExpectRead(ZX_OK, 1)               // Pushed.
        .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW) // Turn the polarity.
        .ExpectRead(ZX_OK, 1);                       // Still pushed, ok to continue.

    // Matrix Scan for 0.
    mock_gpios[2].ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                  // Float column.
    mock_gpios[0].ExpectRead(ZX_OK, 1);                                 // Read row.
    mock_gpios[2].ExpectConfigOut(ZX_OK, gpios_matrix[2].output_value); // Restore column.

    // Matrix Scan for 1.
    mock_gpios[2].ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                  // Float column.
    mock_gpios[1].ExpectRead(ZX_OK, 0);                                 // Read row.
    mock_gpios[2].ExpectConfigOut(ZX_OK, gpios_matrix[2].output_value); // Restore column.

    // Matrix Scan for 2.
    mock_gpios[3].ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                  // Float column.
    mock_gpios[0].ExpectRead(ZX_OK, 0);                                 // Read row.
    mock_gpios[3].ExpectConfigOut(ZX_OK, gpios_matrix[3].output_value); // Restore column.

    // Matrix Scan for 3.
    mock_gpios[3].ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                  // Float column.
    mock_gpios[1].ExpectRead(ZX_OK, 0);                                 // Read row.
    mock_gpios[3].ExpectConfigOut(ZX_OK, gpios_matrix[3].output_value); // Restore colument.

    hidbus_ifc_protocol_ops_t ops = {};
    ops.io_queue = [](void* ctx, const void* buffer, size_t size) {
        buttons_input_rpt_t report_volume_up = {};
        report_volume_up.rpt_id = 1;
        report_volume_up.volume_up = 1;
        ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
        EXPECT_EQ(size, sizeof(report_volume_up));
    };
    hidbus_ifc_protocol_t protocol = {};
    protocol.ops = &ops;
    device.HidbusStart(&protocol);
    device.FakeInterrupt();
    device.ShutDownTest();
    for (size_t i = 0; i < countof(gpios_matrix); ++i) {
        ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
    }
}

} // namespace buttons
