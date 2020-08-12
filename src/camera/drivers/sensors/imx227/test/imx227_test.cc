// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/sensors/imx227/imx227.h"

#include <endian.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <mock/ddktl/protocol/clock.h>
#include <mock/ddktl/protocol/gpio.h>
#include <mock/ddktl/protocol/mipicsi.h>
#include <zxtest/zxtest.h>

#include "src/camera/drivers/sensors/imx227/constants.h"
#include "src/camera/drivers/sensors/imx227/imx227_id.h"
#include "src/camera/drivers/sensors/imx227/mipi_ccs_regs.h"

// The following equality operators are necessary for mocks.

bool operator==(const i2c_op_t& lhs, const i2c_op_t& rhs) { return true; }

bool operator==(const resolution_t& lhs, const resolution_t& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

bool operator==(const mipi_adap_info_t& lhs, const mipi_adap_info_t& rhs) {
  return lhs.resolution == rhs.resolution && lhs.format == rhs.format && lhs.mode == rhs.mode &&
         lhs.path == rhs.path;
}

bool operator==(const mipi_info_t& lhs, const mipi_info_t& rhs) {
  return lhs.channel == rhs.channel && lhs.lanes == rhs.lanes && lhs.ui_value == rhs.ui_value &&
         lhs.csi_version == rhs.csi_version;
}

namespace camera {
namespace {

const uint32_t kTestMode0 = 0;
const uint32_t kTestMode1 = 1;

std::vector<uint8_t> SplitBytes(uint16_t bytes) {
  return std::vector<uint8_t>{static_cast<uint8_t>(bytes >> 8), static_cast<uint8_t>(bytes & 0xff)};
}

class FakeImx227Device : public Imx227Device {
 public:
  FakeImx227Device()
      : Imx227Device(fake_ddk::FakeParent(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
        proto_({&camera_sensor2_protocol_ops_, this}) {
    SetProtocols();
    ExpectInitPdev();
    ASSERT_OK(InitPdev());
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  void ExpectInitPdev() {
    mock_gpio_cam_rst_.ExpectConfigOut(ZX_OK, 1);
    mock_gpio_vana_enable_.ExpectConfigOut(ZX_OK, 0);
    mock_gpio_vdig_enable_.ExpectConfigOut(ZX_OK, 0);
  }

  void ExpectInit() {
    mock_gpio_vana_enable_.ExpectWrite(ZX_OK, true);
    mock_gpio_vdig_enable_.ExpectWrite(ZX_OK, true);
    mock_clk24_.ExpectEnable(ZX_OK);
    mock_gpio_cam_rst_.ExpectWrite(ZX_OK, false);
  }

  void ExpectDeInit() {
    mock_mipi_.ExpectDeInit(ZX_OK);
    mock_gpio_cam_rst_.ExpectWrite(ZX_OK, true);
    mock_clk24_.ExpectDisable(ZX_OK);
    mock_gpio_vdig_enable_.ExpectWrite(ZX_OK, false);
    mock_gpio_vana_enable_.ExpectWrite(ZX_OK, false);
  }

  void ExpectGetSensorId() {
    const auto kSensorModelIdHiRegByteVec = SplitBytes(htobe16(kSensorModelIdReg));
    const auto kSensorModelIdLoRegByteVec = SplitBytes(htobe16(kSensorModelIdReg + 1));
    const auto kSensorModelIdDefaultByteVec = SplitBytes(kSensorModelIdDefault);
    // An I2C bus read is a write of the address followed by a read of the data.
    // In this case, there are two 8-bit reads occuring to get the full 16-bit Sensor Model ID.
    mock_i2c_.ExpectWrite({kSensorModelIdHiRegByteVec[1], kSensorModelIdHiRegByteVec[0]})
        .ExpectReadStop({kSensorModelIdDefaultByteVec[0]})
        .ExpectWrite({kSensorModelIdLoRegByteVec[1], kSensorModelIdLoRegByteVec[0]})
        .ExpectReadStop({kSensorModelIdDefaultByteVec[1]});
  }

  void ExpectGetTestPatternMode(const uint16_t mode) {
    const auto kSensorTestPatternRegByteVec = SplitBytes(htobe16(kTestPatternReg));
    const auto kSensorTestPatternModeByteVec = SplitBytes(htobe16(mode));
    mock_i2c_.ExpectWrite({kSensorTestPatternRegByteVec[1], kSensorTestPatternRegByteVec[0]})
        .ExpectReadStop({kSensorTestPatternModeByteVec[0]});
  }

  void ExpectSetTestPatternMode(const uint16_t mode) {
    const auto kSensorTestPatternMode = SplitBytes(htobe16(kTestPatternReg));
    const auto kSensorTestPatternModeByteVec = SplitBytes(htobe16(mode));
    mock_i2c_.ExpectWrite({kSensorTestPatternMode[1], kSensorTestPatternMode[0]})
        .ExpectWriteStop({kSensorTestPatternModeByteVec[0]});
  }

  void SetProtocols() {
    i2c_ = ddk::I2cChannel(mock_i2c_.GetProto());
    gpio_vana_enable_ = ddk::GpioProtocolClient(mock_gpio_vana_enable_.GetProto());
    gpio_vdig_enable_ = ddk::GpioProtocolClient(mock_gpio_vdig_enable_.GetProto());
    gpio_cam_rst_ = ddk::GpioProtocolClient(mock_gpio_cam_rst_.GetProto());
    clk24_ = ddk::ClockProtocolClient(mock_clk24_.GetProto());
    mipi_ = ddk::MipiCsiProtocolClient(mock_mipi_.GetProto());
  }

  void VerifyAll() {
    mock_i2c_.VerifyAndClear();
    mock_gpio_vana_enable_.VerifyAndClear();
    mock_gpio_vdig_enable_.VerifyAndClear();
    mock_gpio_cam_rst_.VerifyAndClear();
    mock_clk24_.VerifyAndClear();
    mock_mipi_.VerifyAndClear();
  }

  const camera_sensor2_protocol_t* proto() const { return &proto_; }

 private:
  camera_sensor2_protocol_t proto_;
  mock_i2c::MockI2c mock_i2c_;
  ddk::MockGpio mock_gpio_vana_enable_;
  ddk::MockGpio mock_gpio_vdig_enable_;
  ddk::MockGpio mock_gpio_cam_rst_;
  ddk::MockClock mock_clk24_;
  ddk::MockMipiCsi mock_mipi_;
};

class Imx227DeviceTest : public zxtest::Test {
 public:
  Imx227DeviceTest() {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_CAMERA_SENSOR2,
                    *reinterpret_cast<const fake_ddk::Protocol*>(dut_.proto())};
    ddk_.SetProtocols(std::move(protocols));
  }

  void SetUp() override {
    auto dut = std::make_unique<FakeImx227Device>();
    dut_.ExpectInit();
    dut_.ExpectDeInit();
  }

  void TearDown() override {
    dut().CameraSensor2DeInit();
    ASSERT_NO_FATAL_FAILURES(dut().VerifyAll());
  }

  FakeImx227Device& dut() { return dut_; }

 private:
  fake_ddk::Bind ddk_;
  FakeImx227Device dut_;
};

// Returns the coarse integration time corresponding to the requested |frame_rate| if found in the
// lookup table provided.
static uint32_t GetCoarseMaxIntegrationTime(const frame_rate_info_t* lut, uint32_t size,
                                            uint32_t frame_rate) {
  for (uint32_t i = 0; i < size; i++) {
    auto fps =
        lut[i].frame_rate.frames_per_sec_numerator / lut[i].frame_rate.frames_per_sec_denominator;

    if (frame_rate == fps) {
      return lut[i].max_coarse_integration_time;
    }
  }
  return 0;
}

TEST_F(Imx227DeviceTest, Sanity) { ASSERT_OK(dut().CameraSensor2Init()); }

// TODO(fxbug.dev/50737): The expected I2C operations don't match up with those made by
// CameraSensor2GetSensorId.
TEST_F(Imx227DeviceTest, DISABLED_GetSensorId) {
  dut().ExpectGetSensorId();
  uint32_t out_id;
  ASSERT_OK(dut().CameraSensor2Init());
  ASSERT_OK(dut().CameraSensor2GetSensorId(&out_id));
  ASSERT_EQ(out_id, kSensorModelIdDefault);
}

TEST_F(Imx227DeviceTest, DISABLED_GetSetTestPatternMode) {
  dut().ExpectGetTestPatternMode(kTestMode0);
  dut().ExpectSetTestPatternMode(kTestMode1);
  dut().ExpectGetTestPatternMode(kTestMode1);
  ASSERT_OK(dut().CameraSensor2Init());
  uint16_t out_mode;
  ASSERT_OK(dut().CameraSensor2GetTestPatternMode(&out_mode));
  ASSERT_EQ(out_mode, kTestMode0);
  ASSERT_OK(dut().CameraSensor2SetTestPatternMode(kTestMode1));
  ASSERT_OK(dut().CameraSensor2GetTestPatternMode(&out_mode));
  ASSERT_EQ(out_mode, kTestMode1);
}

TEST_F(Imx227DeviceTest, GetFrameRateCoarseIntLut) {
  extension_value_data_type_t ext_val;
  ASSERT_OK(dut().CameraSensor2Init());
  ASSERT_OK(dut().CameraSensor2GetExtensionValue(FRAME_RATE_COARSE_INT_LUT, &ext_val));
  EXPECT_EQ(
      kMaxCoarseIntegrationTimeFor30fpsInLines,
      GetCoarseMaxIntegrationTime(ext_val.frame_rate_info_value, EXTENSION_VALUE_ARRAY_LEN, 30));
  EXPECT_EQ(
      kMaxCoarseIntegrationTimeFor15fpsInLines,
      GetCoarseMaxIntegrationTime(ext_val.frame_rate_info_value, EXTENSION_VALUE_ARRAY_LEN, 15));
}

}  // namespace
}  // namespace camera
