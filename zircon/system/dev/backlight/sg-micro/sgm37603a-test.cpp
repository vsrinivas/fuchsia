// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sgm37603a.h"

#include <fbl/vector.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <unittest/unittest.h>

namespace backlight {

class MockGpio : public ddk::GpioProtocol<MockGpio> {
public:
    MockGpio() : proto_{&gpio_protocol_ops_, this} {}

    const gpio_protocol_t* proto() const { return &proto_; }
    const fbl::Vector<uint8_t>& calls() const { return calls_; }

    zx_status_t GpioConfigIn(uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; }

    zx_status_t GpioConfigOut(uint8_t initial) {
        calls_.push_back(initial);
        return ZX_OK;
    }

    zx_status_t GpioSetAltFunction(uint64_t function) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t GpioRead(uint8_t* out_value) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t GpioWrite(uint8_t value) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t GpioReleaseInterrupt() { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t GpioSetPolarity(gpio_polarity_t polarity) { return ZX_ERR_NOT_SUPPORTED; }

private:
    gpio_protocol_t proto_;
    fbl::Vector<uint8_t> calls_;
};

class MockSgm37603a : public Sgm37603a {
public:
    MockSgm37603a(ddk::I2cChannel i2c)
        : Sgm37603a(nullptr, std::move(i2c), ddk::GpioProtocolClient()) {}

    zx_status_t EnableBacklight() override {
        enable_called_ = true;
        return ZX_OK;
    }

    zx_status_t DisableBacklight() override {
        disable_called_ = true;
        return ZX_OK;
    }

    void Reset() {
        enable_called_ = false;
        disable_called_ = false;
    }

    bool enable_called() const { return enable_called_; }
    bool disable_called() const { return disable_called_; }

private:
    bool enable_called_ = false;
    bool disable_called_ = false;
};

bool TestEnable() {
    BEGIN_TEST;

    mock_i2c::MockI2c mock_i2c;
    mock_i2c
        .ExpectWriteStop({0x10, 0x03})
        .ExpectWriteStop({0x11, 0x00})
        .ExpectWriteStop({0x1a, 0x00})
        .ExpectWriteStop({0x19, 0x00});

    MockGpio mock_gpio;

    Sgm37603a test(nullptr, ddk::I2cChannel(mock_i2c.GetProto()),
                   ddk::GpioProtocolClient(mock_gpio.proto()));
    EXPECT_EQ(ZX_OK, test.EnableBacklight());

    mock_i2c.VerifyAndClear();

    ASSERT_EQ(1, mock_gpio.calls().size());
    EXPECT_EQ(1, mock_gpio.calls()[0]);

    END_TEST;
}

bool TestDisable() {
    BEGIN_TEST;

    mock_i2c::MockI2c mock_i2c;
    MockGpio mock_gpio;

    Sgm37603a test(nullptr, ddk::I2cChannel(mock_i2c.GetProto()),
                   ddk::GpioProtocolClient(mock_gpio.proto()));
    EXPECT_EQ(ZX_OK, test.DisableBacklight());
    ASSERT_EQ(1, mock_gpio.calls().size());
    EXPECT_EQ(0, mock_gpio.calls()[0]);

    END_TEST;
}

bool TestBrightness() {
    BEGIN_TEST;

    mock_i2c::MockI2c mock_i2c;
    MockSgm37603a test(ddk::I2cChannel(mock_i2c.GetProto()));

    EXPECT_EQ(ZX_OK, test.SetBacklightState(false, 127));
    EXPECT_TRUE(test.disable_called());

    test.Reset();
    mock_i2c.VerifyAndClear();

    bool power = true;
    uint8_t brightness = 255;

    EXPECT_EQ(ZX_OK, test.GetBacklightState(&power, &brightness));
    EXPECT_FALSE(power);
    EXPECT_EQ(0, brightness);

    mock_i2c
        .ExpectWriteStop({0x1a, 0})
        .ExpectWriteStop({0x19, 127});

    EXPECT_EQ(ZX_OK, test.SetBacklightState(true, 127));
    EXPECT_TRUE(test.enable_called());

    test.Reset();
    mock_i2c.VerifyAndClear();

    EXPECT_EQ(ZX_OK, test.GetBacklightState(&power, &brightness));
    EXPECT_TRUE(power);
    EXPECT_EQ(127, brightness);

    mock_i2c
        .ExpectWriteStop({0x1a, 0})
        .ExpectWriteStop({0x19, 0});

    EXPECT_EQ(ZX_OK, test.SetBacklightState(true, 0));
    EXPECT_FALSE(test.enable_called());

    test.Reset();
    mock_i2c.VerifyAndClear();

    EXPECT_EQ(ZX_OK, test.GetBacklightState(&power, &brightness));
    EXPECT_TRUE(power);
    EXPECT_EQ(0, brightness);

    END_TEST;
}

}  // namespace backlight

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : 1;
}

BEGIN_TEST_CASE(Sgm37603aTests)
RUN_TEST_SMALL(backlight::TestEnable)
RUN_TEST_SMALL(backlight::TestDisable)
RUN_TEST_SMALL(backlight::TestBrightness)
END_TEST_CASE(Sgm37603aTests)
