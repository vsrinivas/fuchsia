// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/protocol/gpio.h>
#include <lib/mock-function/mock-function.h>
#include <zxtest/zxtest.h>

namespace mock_gpio {

// This class mocks a GPIO device by providing a gpio_protocol_t. Users can set expectations on how
// the protocol ops are called and what status values they return. After the test, use
// VerifyAndClear to reset the object and verify that all expectations were satisfied. See the
// following example test:
//
// mock_gpio::MockGpio gpio;
// gpio
//     .ExpectConfigOut(ZX_OK, 0)
//     .ExpectWrite(ZX_OK, 1)
//     .ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
//     .ExpectRead(ZX_OK, 0);
//
// SomeDriver dut(gpio.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(gpio.VerifyAndClear());

class MockGpio : ddk::GpioProtocol<MockGpio> {
public:
    MockGpio() : proto_{&gpio_protocol_ops_, this} {}

    const gpio_protocol_t* GetProto() const { return &proto_; }

    MockGpio& ExpectConfigIn(zx_status_t status, uint32_t flags) {
        mock_config_in_.ExpectCall(status, flags);
        return *this;
    }

    MockGpio& ExpectConfigOut(zx_status_t status, uint8_t initial_value) {
        mock_config_out_.ExpectCall(status, initial_value);
        return *this;
    }

    MockGpio& ExpectSetAltFunction(zx_status_t status, uint64_t function) {
        mock_set_alt_function_.ExpectCall(status, function);
        return *this;
    }

    MockGpio& ExpectRead(zx_status_t status, uint8_t value) {
        mock_read_.ExpectCall({status, value});
        return *this;
    }

    MockGpio& ExpectWrite(zx_status_t status, uint8_t value) {
        mock_write_.ExpectCall(status, value);
        return *this;
    }

    MockGpio& ExpectGetInterrupt(zx_status_t status, uint32_t flags, const zx::interrupt& out_irq) {
        mock_get_interrupt_.ExpectCall({status, out_irq.get()}, flags);
        return *this;
    }

    MockGpio& ExpectReleaseInterrupt(zx_status_t status) {
        mock_release_interrupt_.ExpectCall(status);
        return *this;
    }

    MockGpio& ExpectSetPolarity(zx_status_t status, gpio_polarity_t polarity) {
        mock_set_polarity_.ExpectCall(status, polarity);
        return *this;
    }

    void VerifyAndClear() {
        mock_config_in_.VerifyAndClear();
        mock_config_out_.VerifyAndClear();
        mock_set_alt_function_.VerifyAndClear();
        mock_read_.VerifyAndClear();
        mock_write_.VerifyAndClear();
        mock_get_interrupt_.VerifyAndClear();
        mock_release_interrupt_.VerifyAndClear();
        mock_set_polarity_.VerifyAndClear();
    }

    zx_status_t GpioConfigIn(uint32_t flags) { return mock_config_in_.Call(flags); }

    zx_status_t GpioConfigOut(uint8_t initial_value) {
        return mock_config_out_.Call(initial_value);
    }

    zx_status_t GpioSetAltFunction(uint64_t function) {
        return mock_set_alt_function_.Call(function);
    }

    zx_status_t GpioRead(uint8_t* out_value) {
        std::tuple<zx_status_t, uint8_t> ret = mock_read_.Call();
        *out_value = std::get<1>(ret);
        return std::get<0>(ret);
    }

    zx_status_t GpioWrite(uint8_t value) { return mock_write_.Call(value); }

    zx_status_t GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
        std::tuple<zx_status_t, zx_handle_t> ret = mock_get_interrupt_.Call(flags);
        out_irq->reset(std::get<1>(ret));
        return std::get<0>(ret);
    }

    zx_status_t GpioReleaseInterrupt() { return mock_release_interrupt_.Call(); }

    zx_status_t GpioSetPolarity(gpio_polarity_t polarity) {
        return mock_set_polarity_.Call(polarity);
    }

private:
    const gpio_protocol_t proto_;
    mock_function::MockFunction<zx_status_t, uint32_t> mock_config_in_;
    mock_function::MockFunction<zx_status_t, uint8_t> mock_config_out_;
    mock_function::MockFunction<zx_status_t, uint64_t> mock_set_alt_function_;
    mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>> mock_read_;
    mock_function::MockFunction<zx_status_t, uint8_t> mock_write_;
    mock_function::MockFunction<std::tuple<zx_status_t, zx_handle_t>, uint32_t> mock_get_interrupt_;
    mock_function::MockFunction<zx_status_t> mock_release_interrupt_;
    mock_function::MockFunction<zx_status_t, gpio_polarity_t> mock_set_polarity_;
};

}  // namespace mock_gpio
