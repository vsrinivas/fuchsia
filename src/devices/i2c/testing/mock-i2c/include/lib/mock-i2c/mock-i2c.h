// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_TESTING_MOCK_I2C_INCLUDE_LIB_MOCK_I2C_MOCK_I2C_H_
#define SRC_DEVICES_I2C_TESTING_MOCK_I2C_INCLUDE_LIB_MOCK_I2C_MOCK_I2C_H_

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/zx/status.h>

#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace mock_i2c {

// This class mocks an addressed I2C device by providing a fuchsia.hardware.i2c WireServer
// implementation. Users can set expectations that either return specified data on read or verify
// data on write. After the test, use VerifyAndClear to reset the object and verify that all
// expectations were satisfied. See the following example test:
//
// mock_i2c::MockI2c i2c;
// i2c
//     .ExpectWrite({0x18})
//     .ExpectReadStop({0x3a, 0x07})
//     .ExpectWriteStop({0x18, 0x57});
//
// auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
// fidl::BindServer(dispatcher(), std::move(endpoints->server), &i2c);
// SomeDriver dut(std::move(endpoints->client));
//
// EXPECT_OK(dut.SomeMethod());
// EXPECT_TRUE(i2c.VerifyAndClear());

class MockI2c : public fidl::WireServer<fuchsia_hardware_i2c::Device> {
 public:
  // Checks that the next operation is a read and returns the contents of buf.
  MockI2c& ExpectRead(fbl::Vector<uint8_t> buf, zx_status_t status = ZX_OK) {
    I2cExpectation exp{std::move(buf), true, false, status};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  // Checks that the next operation is a read followed by a stop and returns the contents of buf.
  MockI2c& ExpectReadStop(fbl::Vector<uint8_t> buf, zx_status_t status = ZX_OK) {
    I2cExpectation exp{std::move(buf), true, true, status};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  // Checks that the next operation is a write and that the data matches the contents of buf.
  MockI2c& ExpectWrite(fbl::Vector<uint8_t> buf, zx_status_t status = ZX_OK) {
    I2cExpectation exp{std::move(buf), false, false, status};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  // Checks that the next operation is a write followed by a stop and that the data matches the
  // contents of buf.
  MockI2c& ExpectWriteStop(fbl::Vector<uint8_t> buf, zx_status_t status = ZX_OK) {
    I2cExpectation exp{std::move(buf), false, true, status};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  void VerifyAndClear() {
    EXPECT_EQ(expectations_index_, expectations_.size(), "There are more transactions expected");
    expectations_.reset();
    expectations_index_ = 0;
  }

  void Transfer(TransferRequestView request, TransferCompleter::Sync& completer) override {
    fidl::Arena arena;
    fbl::Vector<fidl::VectorView<uint8_t>> read_ops;
    zx_status_t status = ZX_OK;

    for (auto it = request->transactions.cbegin(); it != request->transactions.cend();) {
      CheckI2cOp(*it++, [&](const I2cExpectation& exp) {
        if (exp.is_read) {
          fidl::VectorView<uint8_t> read_data(arena, exp.data.size());
          memcpy(read_data.data(), exp.data.data(), exp.data.size());
          read_ops.push_back(read_data);
        }
        status = exp.status;
        return it == request->transactions.cend();
      });
    }

    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }

    fidl::ObjectView<fuchsia_hardware_i2c::wire::DeviceTransferResponse> response(arena);
    response->read_data =
        fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(read_ops.data(), read_ops.size());
    completer.Reply(::fit::ok(response.get()));
  }

 private:
  struct I2cExpectation {
    fbl::Vector<uint8_t> data;
    bool is_read;
    bool stop;
    zx_status_t status;
  };

  // Callback for Transfer to process an I2C expectation and copy read data out if needed. Returns
  // true if this is the last Transaction in the list -- in this case the stop flag should be
  // considered set regardless of its actual value in the table.
  using ProcessI2cExpectation = fit::callback<bool(const I2cExpectation&)>;

  void CheckI2cOp(const fuchsia_hardware_i2c::wire::Transaction& op,
                  ProcessI2cExpectation exp_callback) {
    ASSERT_FALSE(expectations_.is_empty(), "No more transactions are expected");
    ASSERT_LT(expectations_index_, expectations_.size(), "No more transactions are expected");

    ASSERT_TRUE(op.has_data_transfer(), "Transaction has no data transfer field");
    ASSERT_TRUE(op.data_transfer().is_write_data() || op.data_transfer().is_read_size(),
                "Transaction has no write data or read size");

    const bool is_read = op.data_transfer().is_read_size();
    const size_t data_size =
        is_read ? op.data_transfer().read_size() : op.data_transfer().write_data().count();

    const I2cExpectation& exp = expectations_[expectations_index_++];
    const bool override_last_stop = exp_callback(exp);

    const bool stop = (op.has_stop() ? op.stop() : false) || override_last_stop;

    EXPECT_EQ(exp.stop, stop, "Transaction stop flag mismatch");
    ASSERT_EQ(exp.is_read, is_read, "Transaction read flag mismatch");
    ASSERT_EQ(exp.data.size(), data_size, "Transaction size mismatch");

    if (!is_read) {
      EXPECT_BYTES_EQ(exp.data.data(), op.data_transfer().write_data().data(), exp.data.size(),
                      "Transaction data mismatch");
    }
  }

  fbl::Vector<I2cExpectation> expectations_;
  size_t expectations_index_ = 0;
};

}  // namespace mock_i2c

#endif  // SRC_DEVICES_I2C_TESTING_MOCK_I2C_INCLUDE_LIB_MOCK_I2C_MOCK_I2C_H_
