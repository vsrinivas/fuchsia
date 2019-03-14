// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ltr-578als.h"

#include <fbl/vector.h>
#include <lib/mock-hidbus-ifc/mock-hidbus-ifc.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/sync/completion.h>
#include <zxtest/zxtest.h>

namespace light {

TEST(LightTest, Init) {
    zx::port port;
    ASSERT_OK(zx::port::create(0, &port));

    mock_i2c::MockI2c mock_i2c;
    mock_i2c
        .ExpectWriteStop({0x00, 0x03})
        .ExpectWriteStop({0x01, 0x36})
        .ExpectWriteStop({0x02, 0x10})
        .ExpectWriteStop({0x03, 0x1c})
        .ExpectWriteStop({0x04, 0x22})
        .ExpectWriteStop({0x05, 0x00});

    ddk::I2cChannel i2c(mock_i2c.GetProto());
    Ltr578Als device(nullptr, std::move(i2c), std::move(port));

    EXPECT_OK(device.Init());
    mock_i2c.VerifyAndClear();
}

TEST(LightTest, InputReport) {
    zx::port port;
    ASSERT_OK(zx::port::create(0, &port));

    mock_i2c::MockI2c mock_i2c;
    mock_i2c
        .ExpectWrite({0x0d})
        .ExpectReadStop({0xdf, 0x52, 0xd6})
        .ExpectWrite({0x08})
        .ExpectReadStop({0x5d, 0x12});

    ddk::I2cChannel i2c(mock_i2c.GetProto());
    Ltr578Als device(nullptr, std::move(i2c), std::move(port));

    ltr_578als_input_rpt_t report;
    size_t actual;
    EXPECT_OK(device.HidbusGetReport(HID_REPORT_TYPE_INPUT, LTR_578ALS_RPT_ID_INPUT, &report,
                                            sizeof(report), &actual));
    EXPECT_EQ(sizeof(report), actual);

    EXPECT_EQ(LTR_578ALS_RPT_ID_INPUT, report.rpt_id);

    EXPECT_EQ(0xd652df, report.ambient_light);
    EXPECT_EQ(0x125d, report.proximity);

    mock_i2c.VerifyAndClear();
}

TEST(LightTest, FeatureReport) {
    zx::port port;
    ASSERT_OK(zx::port::create(0, &port));

    mock_i2c::MockI2c mock_i2c;
    ddk::I2cChannel i2c(mock_i2c.GetProto());
    Ltr578Als device(nullptr, std::move(i2c), std::move(port));

    ltr_578als_feature_rpt_t report;
    size_t actual;

    EXPECT_OK(device.HidbusGetReport(HID_REPORT_TYPE_FEATURE, LTR_578ALS_RPT_ID_FEATURE,
                                            &report, sizeof(report), &actual));
    EXPECT_EQ(sizeof(report), actual);

    EXPECT_EQ(LTR_578ALS_RPT_ID_FEATURE, report.rpt_id);
    EXPECT_EQ(0, report.interval_ms);

    report.interval_ms = 1000;

    EXPECT_OK(device.HidbusSetReport(HID_REPORT_TYPE_FEATURE, LTR_578ALS_RPT_ID_FEATURE,
                                            &report, sizeof(report)));

    EXPECT_OK(device.HidbusGetReport(HID_REPORT_TYPE_FEATURE, LTR_578ALS_RPT_ID_FEATURE,
                                            &report, sizeof(report), &actual));
    EXPECT_EQ(sizeof(report), actual);

    EXPECT_EQ(LTR_578ALS_RPT_ID_FEATURE, report.rpt_id);
    EXPECT_EQ(1000, report.interval_ms);
}

TEST(LightTest, Polling) {
    zx::port port;
    ASSERT_OK(zx::port::create(0, &port));

    mock_i2c::MockI2c mock_i2c;
    mock_i2c
        .ExpectWrite({0x0d})
        .ExpectReadStop({0xdb, 0xcc, 0x74})
        .ExpectWrite({0x08})
        .ExpectReadStop({0xb0, 0xf9})
        .ExpectWrite({0x0d})
        .ExpectReadStop({0x5c, 0x87, 0xf2})
        .ExpectWrite({0x08})
        .ExpectReadStop({0xe7, 0x04})
        .ExpectWrite({0x0d})
        .ExpectReadStop({0x4e, 0x90, 0x3f})
        .ExpectWrite({0x08})
        .ExpectReadStop({0x31, 0xec});

    ddk::I2cChannel i2c(mock_i2c.GetProto());
    Ltr578Als device(nullptr, std::move(i2c), std::move(port));

    ltr_578als_feature_rpt_t report = {LTR_578ALS_RPT_ID_FEATURE, 1000};
    EXPECT_OK(device.HidbusSetReport(HID_REPORT_TYPE_FEATURE, LTR_578ALS_RPT_ID_FEATURE,
                                            &report, sizeof(report)));

    mock_hidbus_ifc::MockHidbusIfc<ltr_578als_input_rpt_t> mock_ifc;
    EXPECT_OK(device.HidbusStart(mock_ifc.proto()));

    EXPECT_OK(mock_ifc.WaitForReports(3));
    device.HidbusStop();

    ASSERT_NO_FATAL_FAILURES(mock_i2c.VerifyAndClear());

    ASSERT_EQ(3, mock_ifc.reports().size());

    EXPECT_EQ(LTR_578ALS_RPT_ID_INPUT, mock_ifc.reports()[0].rpt_id);
    EXPECT_EQ(0x74ccdb, mock_ifc.reports()[0].ambient_light);
    EXPECT_EQ(0xf9b0, mock_ifc.reports()[0].proximity);

    EXPECT_EQ(LTR_578ALS_RPT_ID_INPUT, mock_ifc.reports()[1].rpt_id);
    EXPECT_EQ(0xf2875c, mock_ifc.reports()[1].ambient_light);
    EXPECT_EQ(0x04e7, mock_ifc.reports()[1].proximity);

    EXPECT_EQ(LTR_578ALS_RPT_ID_INPUT, mock_ifc.reports()[2].rpt_id);
    EXPECT_EQ(0x3f904e, mock_ifc.reports()[2].ambient_light);
    EXPECT_EQ(0xec31, mock_ifc.reports()[2].proximity);
}

TEST(LightTest, NotImplemented) {
    zx::port port;
    ASSERT_OK(zx::port::create(0, &port));

    mock_i2c::MockI2c mock_i2c;
    ddk::I2cChannel i2c(mock_i2c.GetProto());
    Ltr578Als device(nullptr, std::move(i2c), std::move(port));

    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.HidbusGetIdle(0, nullptr));
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.HidbusSetIdle(0, 0));
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.HidbusGetProtocol(nullptr));
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.HidbusSetProtocol({}));
}

}  // namespace light
