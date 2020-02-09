// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include <gtest/gtest.h>
#include <src/graphics/drivers/msd-qcom-adreno/src/instructions.h>
#include <src/graphics/drivers/msd-qcom-adreno/src/msd_qcom_device.h>

// Tests for control processor instruction encoding
class TestInstructions : public ::testing::Test {
 public:
  class Writer : public magma::InstructionWriter {
   public:
    void Write32(uint32_t value) override { values_.push_back(value); }

    std::vector<uint32_t> values_;
  };
};

TEST_F(TestInstructions, CpMeInit) {
  std::vector<uint32_t> packet;
  MsdQcomDevice::GetCpInitPacket(packet);

  Writer writer;
  Packet7::write(&writer, Packet7::OpCode::CpMeInit, packet);
  ASSERT_EQ(writer.values_.size(), packet.size() + 1);

  EXPECT_EQ(0x70c80008u, writer.values_[0]) << "0x" << std::hex << writer.values_[0];

  for (uint32_t i = 0; i < packet.size(); i++) {
    EXPECT_EQ(packet[i], writer.values_[i + 1]);
  }
}

TEST_F(TestInstructions, RegisterWrite) {
  Writer writer;
  Packet4::write(&writer, 0xabcd, 0x12345678);
  ASSERT_EQ(writer.values_.size(), 2u);

  EXPECT_EQ(0x48abcd01u, writer.values_[0]) << "0x" << std::hex << writer.values_[0];
  EXPECT_EQ(0x12345678u, writer.values_[1]) << "0x" << std::hex << writer.values_[1];
}
