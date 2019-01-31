// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/vector.h>

namespace mock_i2c {

// This class mocks an addressed I2C device by providing an i2c_protocol_t. Users can set
// expectations that either return specified data on read or verify data on write. After the test,
// use VerifyAndClear to reset the object and verify that all expectations were satisfied. See the
// following example test:
//
// mock_i2c::MockI2c i2c;
// i2c
//     .ExpectWrite({0x18})
//     .ExpectReadStop({0x3a, 0x07})
//     .ExpectWriteStop({0x18, 0x57});
//
// SomeDriver dut(i2c.GetProto());
//
// EXPECT_EQ(dut.SomeMethod(), ZX_OK);
// EXPECT_TRUE(i2c.VerifyAndClear());

class MockI2c : ddk::I2cProtocol<MockI2c> {
public:
    MockI2c() : proto_{&i2c_protocol_ops_, this} {}

    const i2c_protocol_t* GetProto() { return &proto_; }

    // Checks that the next operation is a read and returns the contents of buf.
    MockI2c& ExpectRead(fbl::Vector<uint8_t> buf);

    // Checks that the next operation is a read followed by a stop and returns the contents of buf.
    MockI2c& ExpectReadStop(fbl::Vector<uint8_t> buf);

    // Checks that the next operation is a write and that the data matches the contents of buf.
    MockI2c& ExpectWrite(fbl::Vector<uint8_t> buf);

    // Checks that the next operation is a write followed by a stop and that the data matches the
    // contents of buf.
    MockI2c& ExpectWriteStop(fbl::Vector<uint8_t> buf);

    bool VerifyAndClear();

    // These are used by ddk::I2cProtocol but are not intended for use by tests.
    void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                     void* cookie);
    zx_status_t I2cGetMaxTransferSize(size_t* out_size);
    zx_status_t I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq);

private:
    struct I2cExpectation {
        fbl::Vector<uint8_t> data;
        bool is_read;
        bool stop;
    };

    bool CheckI2cOp(const i2c_op_t& op, fbl::Vector<i2c_op_t>* read_ops);

    const i2c_protocol_t proto_;
    fbl::Vector<I2cExpectation> expectations_;
    size_t expectations_index_ = 0;
};

}  // namespace mock_i2c
