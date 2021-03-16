// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftdi-i2c.h"

#include <fuchsia/hardware/ftdi/llcpp/fidl.h>
#include <fuchsia/hardware/serialimpl/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <stdio.h>

#include <list>

#include <zxtest/zxtest.h>

#include "ftdi.h"

namespace ftdi_mpsse {

// Fake for the raw nand protocol.
class FakeSerial : public ddk::SerialImplProtocol<FakeSerial> {
 public:
  FakeSerial() : proto_({&serial_impl_protocol_ops_, this}) {}

  const serial_impl_protocol_t* proto() const { return &proto_; }

  void PushExpectedRead(std::vector<uint8_t> read) { expected_reads_.push_back(std::move(read)); }

  void PushExpectedWrite(std::vector<uint8_t> write) {
    expected_writes_.push_back(std::move(write));
  }

  void FailOnUnexpectedReadWrite(bool b) { unexpected_is_error_ = b; }

  zx_status_t SerialImplGetInfo(serial_port_info_t* out_info) { return ZX_OK; }

  zx_status_t SerialImplConfig(uint32_t baud_rate, uint32_t flags) { return ZX_OK; }

  zx_status_t SerialImplEnable(bool enable) { return ZX_OK; }

  zx_status_t SerialImplRead(uint8_t* out_buf_buffer, size_t buf_size, size_t* out_buf_actual) {
    uint8_t* out_buf = out_buf_buffer;
    if (expected_reads_.size() == 0) {
      if (unexpected_is_error_) {
        printf("Read with no expected read set\n");
        return ZX_ERR_INTERNAL;
      } else {
        *out_buf_actual = buf_size;
        return ZX_OK;
      }
    }
    std::vector<uint8_t>& read = expected_reads_.front();
    if (buf_size != read.size()) {
      printf("Read size mismatch (%lx != %lx\n", buf_size, read.size());
      return ZX_ERR_INTERNAL;
    }

    for (size_t i = 0; i < buf_size; i++) {
      out_buf[i] = read[i];
    }

    expected_reads_.pop_front();
    *out_buf_actual = buf_size;
    return ZX_OK;
  }

  zx_status_t SerialImplWrite(const uint8_t* buf_buffer, size_t buf_size, size_t* out_actual) {
    const uint8_t* out_buf = buf_buffer;
    if (expected_writes_.size() == 0) {
      if (unexpected_is_error_) {
        printf("Write with no expected wrte set\n");
        return ZX_ERR_INTERNAL;
      } else {
        *out_actual = buf_size;
        return ZX_OK;
      }
    }
    std::vector<uint8_t>& write = expected_writes_.front();
    if (buf_size != write.size()) {
      printf("Write size mismatch (0x%lx != 0x%lx\n", buf_size, write.size());
      return ZX_ERR_INTERNAL;
    }

    for (size_t i = 0; i < buf_size; i++) {
      if (out_buf[i] != write[i]) {
        printf("Write data mismatch index %ld (0x%x != 0x%x)\n", i, out_buf[i], write[i]);
        return ZX_ERR_INTERNAL;
      }
    }
    expected_writes_.pop_front();
    *out_actual = buf_size;
    return ZX_OK;
  }

  zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb) { return ZX_OK; }

 private:
  bool unexpected_is_error_ = false;
  std::list<std::vector<uint8_t>> expected_reads_;
  std::list<std::vector<uint8_t>> expected_writes_;

  serial_impl_protocol_t proto_;
};

class FtdiI2cTest : public zxtest::Test {
 public:
  void SetUp() override { ddk_.SetProtocol(ZX_PROTOCOL_SERIAL_IMPL, serial_.proto()); }

 protected:
  FtdiI2c FtdiBasicInit(void) {
    FtdiI2c::I2cLayout layout = {0, 1, 2};
    std::vector<FtdiI2c::I2cDevice> i2c_devices(1);
    i2c_devices[0].address = 0x3c;
    i2c_devices[0].vid = 0;
    i2c_devices[0].pid = 0;
    i2c_devices[0].did = 31;
    return FtdiI2c(fake_ddk::kFakeParent, layout, i2c_devices);
  }

  fake_ddk::Bind ddk_;
  FakeSerial serial_;
};

TEST_F(FtdiI2cTest, TrivialLifetimeTest) { FtdiI2c device = FtdiBasicInit(); }

TEST_F(FtdiI2cTest, DdkLifetimeTest) {
  FtdiI2c::I2cLayout layout = {0, 1, 2};
  std::vector<FtdiI2c::I2cDevice> i2c_devices(1);
  i2c_devices[0].address = 0x3c;
  i2c_devices[0].vid = 0;
  i2c_devices[0].pid = 0;
  i2c_devices[0].did = 31;
  FtdiI2c* device(new FtdiI2c(fake_ddk::kFakeParent, layout, i2c_devices));

  // These Reads and Writes are to sync the device on bind.
  std::vector<uint8_t> first_write(1);
  first_write[0] = 0xAB;
  serial_.PushExpectedWrite(std::move(first_write));

  std::vector<uint8_t> first_read(2);
  first_read[0] = 0xFA;
  first_read[1] = 0xAB;

  serial_.PushExpectedRead(std::move(first_read));

  // Check that bind works.
  ASSERT_OK(device->Bind());
  ASSERT_OK(ddk_.WaitUntilInitComplete());
  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());

  // This should delete the object, which means this test should not leak.
  device->DdkRelease();
}

TEST_F(FtdiI2cTest, DdkLifetimeFailedInit) {
  FtdiI2c::I2cLayout layout = {0, 1, 2};
  std::vector<FtdiI2c::I2cDevice> i2c_devices(1);
  i2c_devices[0].address = 0x3c;
  i2c_devices[0].vid = 0;
  i2c_devices[0].pid = 0;
  i2c_devices[0].did = 31;
  FtdiI2c* device(new FtdiI2c(fake_ddk::kFakeParent, layout, i2c_devices));

  // These Reads and Writes are to sync the device on bind.
  std::vector<uint8_t> first_write(1);
  first_write[0] = 0xAB;
  serial_.PushExpectedWrite(std::move(first_write));

  // Set bad read data. This will cause the enable worker thread to fail.
  std::vector<uint8_t> first_read(2);
  first_read[0] = 0x00;
  first_read[1] = 0x00;

  serial_.PushExpectedRead(std::move(first_read));

  // Bind should spawn the worker thread which will fail the init.
  ASSERT_OK(device->Bind());
  ASSERT_OK(ddk_.WaitUntilInitComplete());
  ASSERT_OK(ddk_.WaitUntilRemove());
  EXPECT_TRUE(ddk_.Ok());

  // This should delete the object, which means this test should not leak.
  device->DdkRelease();
}

TEST_F(FtdiI2cTest, PingTest) {
  FtdiI2c device = FtdiBasicInit();
  std::vector<uint8_t> ping_data = {
      0x80, 0x3, 0x3,  0x82, 0x0, 0x0,  0x80, 0x1, 0x3,  0x82, 0x0,  0x0, 0x80, 0x0,  0x3,  0x82,
      0x0,  0x0, 0x11, 0x0,  0x0, 0x78, 0x80, 0x2, 0x3,  0x82, 0x0,  0x0, 0x22, 0x0,  0x11, 0x0,
      0x0,  0x0, 0x80, 0x2,  0x3, 0x82, 0x0,  0x0, 0x22, 0x0,  0x80, 0x0, 0x3,  0x82, 0x0,  0x0,
      0x80, 0x1, 0x3,  0x82, 0x0, 0x0,  0x80, 0x3, 0x3,  0x82, 0x0,  0x0, 0x87};
  serial_.PushExpectedWrite(std::move(ping_data));

  zx_status_t status = device.Ping(0x3c);
  ASSERT_OK(status);
}

TEST_F(FtdiI2cTest, ReadTest) {
  FtdiI2c device = FtdiBasicInit();

  serial_.FailOnUnexpectedReadWrite(false);
  std::vector<uint8_t> serial_read_data = {
      0x00,  // The ACK for writing bus address.
      0x00,  // The ACK for writing register value.
      0x00,  // The ACK for initiating a read.
      0xDE,  // The Value we will be reading out.
  };
  serial_.PushExpectedRead(std::move(serial_read_data));

  i2c_impl_op_t op_list[2] = {};
  op_list[0].is_read = false;
  op_list[0].stop = false;
  uint8_t write_data = 0xAB;
  op_list[0].data_buffer = &write_data;
  op_list[0].data_size = sizeof(write_data);

  op_list[1].is_read = true;
  op_list[1].stop = true;
  uint8_t read_data = 0;
  op_list[1].data_buffer = &read_data;
  op_list[1].data_size = sizeof(read_data);

  zx_status_t status = device.I2cImplTransact(0, op_list, 2);
  ASSERT_OK(status);
  ASSERT_EQ(0xDE, read_data);
}

TEST_F(FtdiI2cTest, NackReadTest) {
  FtdiI2c device = FtdiBasicInit();

  serial_.FailOnUnexpectedReadWrite(false);
  std::vector<uint8_t> serial_read_data = {
      0x01,  // The NACK for writing bus address.
      0x01,  // The NACK for writing register value.
      0x01,  // The NACK for initiating a read.
      0x00,  // The Value we will be reading out.
  };
  serial_.PushExpectedRead(std::move(serial_read_data));

  i2c_impl_op_t op_list[2] = {};
  op_list[0].is_read = false;
  op_list[0].stop = false;
  uint8_t write_data = 0xAB;
  op_list[0].data_buffer = &write_data;
  op_list[0].data_size = sizeof(write_data);

  op_list[1].is_read = true;
  op_list[1].stop = true;
  uint8_t read_data = 0;
  op_list[1].data_buffer = &read_data;
  op_list[1].data_size = sizeof(read_data);

  zx_status_t status = device.I2cImplTransact(0, op_list, 2);
  ASSERT_EQ(ZX_ERR_INTERNAL, status);
}

}  // namespace ftdi_mpsse
