// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/power/drivers/fusb302/fusb302.h"

#include <lib/mock-i2c/mock-i2c.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/inspect/testing/cpp/zxtest/inspect.h"
#include "src/devices/power/drivers/fusb302/usb-pd.h"

namespace fusb302 {

using inspect::InspectTestHelper;
using usb::pd::DataPdMessage;
using DataMessageType = usb::pd::DataPdMessage::DataMessageType;

class Fusb302Test : public Fusb302 {
 public:
  Fusb302Test(const i2c_protocol_t* i2c) : Fusb302(nullptr, i2c, {}) {}  // TODO

  zx_status_t Init() {
    // Test version of Init doesn't start IrqThread
    auto status = InitInspect();
    if (status != ZX_OK) {
      return status;
    }
    return InitHw();
  }
  zx_status_t FifoTransmit(const PdMessage& message) { return Fusb302::FifoTransmit(message); }
  zx::status<PdMessage> FifoReceive() { return Fusb302::FifoReceive(); }

  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }
};

class Fusb302TestFixture : public InspectTestHelper, public zxtest::Test {
 public:
  Fusb302TestFixture() : dut_(mock_i2c_.GetProto()) {}

  void SetUp() override {
    // InitInspect
    mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0x91});  // Device ID

    // InitHw
    mock_i2c_.ExpectWrite({0x0C}).ExpectReadStop({0x00});  // Reset
    mock_i2c_.ExpectWriteStop({0x0C, 0x03});
    mock_i2c_.ExpectWrite({0x09}).ExpectReadStop({0x00});  // Control3Reg
    mock_i2c_.ExpectWriteStop({0x09, 0x7});
    mock_i2c_.ExpectWriteStop({0x0A, 0x74});               // MaskReg
    mock_i2c_.ExpectWriteStop({0x0E, 0xA2});               // MaskAReg
    mock_i2c_.ExpectWriteStop({0x0F, 0xFE});               // MaskBReg
    mock_i2c_.ExpectWrite({0x08}).ExpectReadStop({0x00});  // Control2Reg
    mock_i2c_.ExpectWriteStop({0x08, 0x23});
    mock_i2c_.ExpectWrite({0x06}).ExpectReadStop({0x20});  // Control0Reg
    mock_i2c_.ExpectWriteStop({0x06, 0x08});
    {
      // SetPolarity(CC1)
      mock_i2c_.ExpectWrite({0x02}).ExpectReadStop({0x00});  // Switches0Reg
      mock_i2c_.ExpectWriteStop({0x02, 0x04});
      mock_i2c_.ExpectWrite({0x03}).ExpectReadStop({0x00});  // Switches1Reg
      mock_i2c_.ExpectWriteStop({0x03, 0x01});
    }
    mock_i2c_.ExpectWriteStop({0x0B, 0x0F});
    {
      // RxEnable(false)
      {
        // SetCC(DRP)
        mock_i2c_.ExpectWrite({0x02}).ExpectReadStop({0xFF});  // Switches0Reg
        mock_i2c_.ExpectWriteStop({0x02, 0x3F});
      }
      mock_i2c_.ExpectWrite({0x08}).ExpectReadStop({0x00});  // Control2Reg
      mock_i2c_.ExpectWriteStop({0x08, 0x20});
      mock_i2c_.ExpectWrite({0x02}).ExpectReadStop({0xFF});  // Switches0Reg
      mock_i2c_.ExpectWriteStop({0x02, 0xF3});
      mock_i2c_.ExpectWrite({0x03}).ExpectReadStop({0xFF});  // Switches1Reg
      mock_i2c_.ExpectWriteStop({0x03, 0xFB});
    }
    {
      // SetCC(DRP)
      mock_i2c_.ExpectWrite({0x02}).ExpectReadStop({0xFF});  // Switches0Reg
      mock_i2c_.ExpectWriteStop({0x02, 0x3F});
    }

    EXPECT_OK(dut_.Init());
  }

  void TearDown() override { mock_i2c_.VerifyAndClear(); }

 protected:
  mock_i2c::MockI2c mock_i2c_;
  Fusb302Test dut_;
};

TEST_F(Fusb302TestFixture, InspectTest) {
  ASSERT_NO_FATAL_FAILURES(ReadInspect(dut_.inspect_vmo()));
  auto* inspect_device_id = hierarchy().GetByPath({"DeviceId"});
  ASSERT_TRUE(inspect_device_id);
  // VersionId: 9
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_device_id->node(), "VersionId", inspect::UintPropertyValue(9)));
  // ProductId: 0
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_device_id->node(), "ProductId", inspect::UintPropertyValue(0)));
  // RevisionId: 1
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_device_id->node(), "RevisionId", inspect::UintPropertyValue(1)));

  auto* inspect_sink_policy_engine = hierarchy().GetByPath({"SinkPolicyEngine"});
  ASSERT_TRUE(inspect_sink_policy_engine);
  // Capabilities
  auto capabilities =
      inspect_sink_policy_engine->node().get_property<inspect::UintArrayValue>("Capabilities");
  EXPECT_TRUE(capabilities);
  // CurrentCapabilityIndex: UINT8_MAX
  ASSERT_NO_FATAL_FAILURES(CheckProperty(inspect_sink_policy_engine->node(),
                                         "CurrentCapabilityIndex",
                                         inspect::UintPropertyValue(UINT8_MAX)));
  // RequestedMaxCurrent_mA: kChargeInputDefaultCur
  ASSERT_NO_FATAL_FAILURES(CheckProperty(inspect_sink_policy_engine->node(),
                                         "RequestedMaxCurrent_mA",
                                         inspect::UintPropertyValue(kChargeInputDefaultCur)));
  // RequestedMaxVoltage_mV: kChargeInputDefaultVol
  ASSERT_NO_FATAL_FAILURES(CheckProperty(inspect_sink_policy_engine->node(),
                                         "RequestedMaxVoltage_mV",
                                         inspect::UintPropertyValue(kChargeInputDefaultVol)));

  auto* inspect_state_machine = hierarchy().GetByPath({"StateMachine"});
  ASSERT_TRUE(inspect_state_machine);
  // State: 0 (disabled)
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_state_machine->node(), "State", inspect::UintPropertyValue(0)));

  auto* inspect_hw_drp = hierarchy().GetByPath({"HardwareDRP"});
  ASSERT_TRUE(inspect_hw_drp);
  // PowerRole: false (sink)
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_hw_drp->node(), "PowerRole", inspect::BoolPropertyValue(false)));
  // DataRole: 1 (UFP)
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_hw_drp->node(), "DataRole", inspect::UintPropertyValue(1)));
  // SpecRev: 01 (kRev2)
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_hw_drp->node(), "SpecRev", inspect::UintPropertyValue(1)));
  // Polarity: false (CC1)
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_hw_drp->node(), "Polarity", inspect::BoolPropertyValue(false)));
  // TxState: 2 (success)
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(inspect_hw_drp->node(), "TxState", inspect::UintPropertyValue(2)));
}

TEST_F(Fusb302TestFixture, FifoTransmitTest) {
  uint8_t expected[] = {0x12, 0x12, 0x12, 0x13, 0x86, 0x62, 0x18, 0x12,
                        0x34, 0x56, 0x78, 0xFF, 0x14, 0xFE, 0xA1};
  for (unsigned char& i : expected) {
    mock_i2c_.ExpectWriteStop({0x43, i});
  }
  uint8_t payload[4] = {0x12, 0x34, 0x56, 0x78};
  DataPdMessage message(/* num_data_objects */ 1, /* message_id */ 4,
                        /* power_role: sink */ false, SpecRev::kRev2,
                        /* data_role: UFP */ true, DataMessageType::REQUEST, payload);
  EXPECT_OK(dut_.FifoTransmit(message));
}

TEST_F(Fusb302TestFixture, FifoReceiveTest) {
  uint8_t expected[] = {0xE0, 0x42, 0x10, 0x12, 0x34, 0x56, 0x78, 0x87, 0x65, 0x43, 0x21};
  for (unsigned char& i : expected) {
    mock_i2c_.ExpectWrite({0x43}).ExpectReadStop({i});
  }
  auto message = dut_.FifoReceive();
  EXPECT_OK(message.status_value());
  EXPECT_EQ(message->header().value, 0x1042);
  EXPECT_EQ(message->payload()[0], 0x12);
  EXPECT_EQ(message->payload()[1], 0x34);
  EXPECT_EQ(message->payload()[2], 0x56);
  EXPECT_EQ(message->payload()[3], 0x78);
}

}  // namespace fusb302
