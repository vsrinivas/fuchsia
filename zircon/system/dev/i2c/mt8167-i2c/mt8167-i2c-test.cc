// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167-i2c.h"

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

namespace mt8167_i2c {

class Mt8167I2cTest : public Mt8167I2c {
 public:
  Mt8167I2cTest(uint32_t bus_count) : Mt8167I2c(nullptr) { bus_count_ = bus_count; }

  auto& mock_I2cImplTransact() { return mock_i2c_impl_transact_; }

  void VerifyAll() { mock_i2c_impl_transact_.VerifyAndClear(); }

  void SetI2cGpios(const ddk::MockGpio gpios[], size_t count) {
    fbl::AllocChecker ac;
    gpios_.reset(new (&ac) ddk::GpioProtocolClient[count], count);
    ASSERT_TRUE(ac.check());

    for (size_t i = 0; i < count; i++) {
      gpios_[i] = ddk::GpioProtocolClient(gpios[i].GetProto());
    }
  }
  void Reset(uint32_t id) override { reset_called_ = true; }

  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* ops, size_t count) override {
    return mock_i2c_impl_transact_.Call(bus_id, count);
  }

  zx_status_t GetI2cGpios(fbl::Array<ddk::GpioProtocolClient>* gpios) override {
    size_t count = gpios_.size();
    gpios->reset(gpios_.release(), count);
    return ZX_OK;
  }

  bool Ok() { return reset_called_; }

 private:
  fbl::Array<ddk::GpioProtocolClient> gpios_;
  mock_function::MockFunction<zx_status_t, uint32_t, size_t> mock_i2c_impl_transact_;
  bool reset_called_ = false;
};

TEST(Mt8167I2cTest, DummyTransactions) {
  Mt8167I2cTest dut(3);

  ddk::MockGpio gpios[6];
  dut.SetI2cGpios(gpios, fbl::count_of(gpios));

  gpios[0].ExpectSetAltFunction(ZX_OK, 0).ExpectSetAltFunction(ZX_OK, 1);
  gpios[1].ExpectSetAltFunction(ZX_OK, 0).ExpectSetAltFunction(ZX_OK, 1);
  gpios[2].ExpectSetAltFunction(ZX_OK, 0).ExpectSetAltFunction(ZX_OK, 1);
  gpios[3].ExpectSetAltFunction(ZX_OK, 0).ExpectSetAltFunction(ZX_OK, 1);
  gpios[4].ExpectSetAltFunction(ZX_OK, 0).ExpectSetAltFunction(ZX_OK, 1);
  gpios[5].ExpectSetAltFunction(ZX_OK, 0).ExpectSetAltFunction(ZX_OK, 1);

  dut.mock_I2cImplTransact()
      .ExpectCall(ZX_OK, 0, 1)
      .ExpectCall(ZX_OK, 1, 1)
      .ExpectCall(ZX_OK, 2, 1);

  EXPECT_OK(dut.DoDummyTransactions());
  EXPECT_TRUE(dut.Ok());

  dut.VerifyAll();
  gpios[0].VerifyAndClear();
  gpios[1].VerifyAndClear();
  gpios[2].VerifyAndClear();
  gpios[3].VerifyAndClear();
  gpios[4].VerifyAndClear();
  gpios[5].VerifyAndClear();
}

}  // namespace mt8167_i2c
