// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bma253.h"

#include <fbl/vector.h>
#include <lib/mock-hidbus-ifc/mock-hidbus-ifc.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/sync/completion.h>
#include <zxtest/zxtest.h>

namespace accel {

TEST(AccelTest, Init) {
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWriteStop({0x0f, 0b0101}).ExpectWriteStop({0x10, 0b01011});

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Bma253 device(nullptr, std::move(i2c), std::move(port));

  EXPECT_OK(device.Init());
  mock_i2c.VerifyAndClear();
}

TEST(AccelTest, InputReport) {
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x02})
      .ExpectReadStop({0xb1, 0x65, 0x31, 0xf7, 0x01, 0x39})
      .ExpectWrite({0x08})
      .ExpectReadStop({0x3e});

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Bma253 device(nullptr, std::move(i2c), std::move(port));

  bma253_input_rpt_t report;
  size_t actual;
  EXPECT_OK(device.HidbusGetReport(HID_REPORT_TYPE_INPUT, BMA253_RPT_ID_INPUT, &report,
                                   sizeof(report), &actual));
  EXPECT_EQ(sizeof(report), actual);

  EXPECT_EQ(BMA253_RPT_ID_INPUT, report.rpt_id);

  // Use memcpy() to avoid a reference to a misaligned address.
  uint16_t acceleration_x, acceleration_y, acceleration_z;
  memcpy(&acceleration_x, &report.acceleration_x, sizeof(acceleration_x));
  memcpy(&acceleration_y, &report.acceleration_y, sizeof(acceleration_y));
  memcpy(&acceleration_z, &report.acceleration_z, sizeof(acceleration_z));
  EXPECT_EQ(0x65b, acceleration_x);
  EXPECT_EQ(0xf73, acceleration_y);
  EXPECT_EQ(0x390, acceleration_z);
  EXPECT_EQ(0x3e, report.temperature);

  mock_i2c.VerifyAndClear();
}

TEST(AccelTest, FeatureReport) {
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  mock_i2c::MockI2c mock_i2c;
  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Bma253 device(nullptr, std::move(i2c), std::move(port));

  bma253_feature_rpt_t report;
  size_t actual;

  EXPECT_OK(device.HidbusGetReport(HID_REPORT_TYPE_FEATURE, BMA253_RPT_ID_FEATURE, &report,
                                   sizeof(report), &actual));
  EXPECT_EQ(sizeof(report), actual);

  EXPECT_EQ(BMA253_RPT_ID_FEATURE, report.rpt_id);

  uint32_t interval_ms;
  memcpy(&interval_ms, &report.interval_ms, sizeof(interval_ms));
  EXPECT_EQ(0, interval_ms);

  report.interval_ms = 1000;

  EXPECT_OK(device.HidbusSetReport(HID_REPORT_TYPE_FEATURE, BMA253_RPT_ID_FEATURE, &report,
                                   sizeof(report)));

  EXPECT_OK(device.HidbusGetReport(HID_REPORT_TYPE_FEATURE, BMA253_RPT_ID_FEATURE, &report,
                                   sizeof(report), &actual));
  EXPECT_EQ(sizeof(report), actual);

  EXPECT_EQ(BMA253_RPT_ID_FEATURE, report.rpt_id);

  memcpy(&interval_ms, &report.interval_ms, sizeof(interval_ms));
  EXPECT_EQ(1000, interval_ms);
}

TEST(AccelTest, Polling) {
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x02})
      .ExpectReadStop({0xb1, 0x65, 0x31, 0xf7, 0x01, 0x39})
      .ExpectWrite({0x08})
      .ExpectReadStop({0x3e})
      .ExpectWrite({0x02})
      .ExpectReadStop({0x91, 0x51, 0xc1, 0x73, 0xa1, 0xd6})
      .ExpectWrite({0x08})
      .ExpectReadStop({0xb6})
      .ExpectWrite({0x02})
      .ExpectReadStop({0x61, 0x24, 0x61, 0xf2, 0xe1, 0x93})
      .ExpectWrite({0x08})
      .ExpectReadStop({0x72});

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Bma253 device(nullptr, std::move(i2c), std::move(port));

  bma253_feature_rpt_t report = {BMA253_RPT_ID_FEATURE, 1000};
  EXPECT_OK(device.HidbusSetReport(HID_REPORT_TYPE_FEATURE, BMA253_RPT_ID_FEATURE, &report,
                                   sizeof(report)));

  mock_hidbus_ifc::MockHidbusIfc<bma253_input_rpt_t> mock_ifc;
  EXPECT_OK(device.HidbusStart(mock_ifc.proto()));

  EXPECT_OK(mock_ifc.WaitForReports(3));
  device.HidbusStop();

  ASSERT_NO_FATAL_FAILURES(mock_i2c.VerifyAndClear());

  ASSERT_EQ(3, mock_ifc.reports().size());

  uint16_t acceleration_x, acceleration_y, acceleration_z;
  memcpy(&acceleration_x, &mock_ifc.reports()[0].acceleration_x, sizeof(acceleration_x));
  memcpy(&acceleration_y, &mock_ifc.reports()[0].acceleration_y, sizeof(acceleration_y));
  memcpy(&acceleration_z, &mock_ifc.reports()[0].acceleration_z, sizeof(acceleration_z));
  EXPECT_EQ(BMA253_RPT_ID_INPUT, mock_ifc.reports()[0].rpt_id);
  EXPECT_EQ(0x65b, acceleration_x);
  EXPECT_EQ(0xf73, acceleration_y);
  EXPECT_EQ(0x390, acceleration_z);
  EXPECT_EQ(0x3e, mock_ifc.reports()[0].temperature);

  memcpy(&acceleration_x, &mock_ifc.reports()[1].acceleration_x, sizeof(acceleration_x));
  memcpy(&acceleration_y, &mock_ifc.reports()[1].acceleration_y, sizeof(acceleration_y));
  memcpy(&acceleration_z, &mock_ifc.reports()[1].acceleration_z, sizeof(acceleration_z));
  EXPECT_EQ(BMA253_RPT_ID_INPUT, mock_ifc.reports()[1].rpt_id);
  EXPECT_EQ(0x519, acceleration_x);
  EXPECT_EQ(0x73c, acceleration_y);
  EXPECT_EQ(0xd6a, acceleration_z);
  EXPECT_EQ(0xb6, mock_ifc.reports()[1].temperature);

  memcpy(&acceleration_x, &mock_ifc.reports()[2].acceleration_x, sizeof(acceleration_x));
  memcpy(&acceleration_y, &mock_ifc.reports()[2].acceleration_y, sizeof(acceleration_y));
  memcpy(&acceleration_z, &mock_ifc.reports()[2].acceleration_z, sizeof(acceleration_z));
  EXPECT_EQ(BMA253_RPT_ID_INPUT, mock_ifc.reports()[2].rpt_id);
  EXPECT_EQ(0x246, acceleration_x);
  EXPECT_EQ(0xf26, acceleration_y);
  EXPECT_EQ(0x93e, acceleration_z);
  EXPECT_EQ(0x72, mock_ifc.reports()[2].temperature);
}

TEST(AccelTest, NotImplemented) {
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  mock_i2c::MockI2c mock_i2c;
  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Bma253 device(nullptr, std::move(i2c), std::move(port));

  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.HidbusGetIdle(0, nullptr));
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.HidbusSetIdle(0, 0));
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.HidbusGetProtocol(nullptr));
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.HidbusSetProtocol({}));
}

}  // namespace accel
