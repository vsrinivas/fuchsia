// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mock-i2c/mock-i2c.h"

#include <unittest/unittest.h>

namespace mock_i2c {

MockI2c& MockI2c::ExpectRead(fbl::Vector<uint8_t> buf) {
    I2cExpectation exp{std::move(buf), true, false};
    expectations_.push_back(std::move(exp));
    return *this;
}

MockI2c& MockI2c::ExpectReadStop(fbl::Vector<uint8_t> buf) {
    I2cExpectation exp{std::move(buf), true, true};
    expectations_.push_back(std::move(exp));
    return *this;
}

MockI2c& MockI2c::ExpectWrite(fbl::Vector<uint8_t> buf) {
    I2cExpectation exp{std::move(buf), false, false};
    expectations_.push_back(std::move(exp));
    return *this;
}

MockI2c& MockI2c::ExpectWriteStop(fbl::Vector<uint8_t> buf) {
    I2cExpectation exp{std::move(buf), false, true};
    expectations_.push_back(std::move(exp));
    return *this;
}

bool MockI2c::VerifyAndClear() {
    BEGIN_HELPER;

    EXPECT_EQ(expectations_index_, expectations_.size(), "There are more transactions expected");
    expectations_.reset();
    expectations_index_ = 0;

    END_HELPER;
}

void MockI2c::I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                          void* cookie) {
    fbl::Vector<i2c_op_t> read_ops;

    bool result = true;
    for (size_t i = 0; i < op_count; i++) {
        if (!(result = CheckI2cOp(op_list[i], &read_ops))) {
            break;
        }
    }

    if (result) {
        callback(cookie, ZX_OK, read_ops.get(), read_ops.size());
    } else {
        callback(cookie, ZX_OK, nullptr, 0);
    }

}

zx_status_t MockI2c::I2cGetMaxTransferSize(size_t* out_size) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockI2c::I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
}

bool MockI2c::CheckI2cOp(const i2c_op_t& op, fbl::Vector<i2c_op_t>* read_ops) {
    BEGIN_HELPER;

    ASSERT_FALSE(expectations_.is_empty(), "No more transactions are expected");
    ASSERT_LT(expectations_index_, expectations_.size(), "No more transactions are expected");

    const I2cExpectation& exp = expectations_[expectations_index_++];
    EXPECT_EQ(exp.stop, op.stop, "Transaction stop flag mismatch");
    ASSERT_EQ(exp.is_read, op.is_read, "Transaction read flag mismatch");
    ASSERT_EQ(exp.data.size(), op.data_size, "Transaction size mismatch");

    if (exp.is_read) {
        read_ops->push_back({
            .data_buffer = exp.data.get(),
            .data_size = exp.data.size(),
            .is_read = true,
            .stop = exp.stop
        });
    } else {
        EXPECT_EQ(0, memcmp(exp.data.get(), op.data_buffer, exp.data.size()),
                  "Transaction data mismatch");
    }

    END_HELPER;
}

}  // namespace mock_i2c
