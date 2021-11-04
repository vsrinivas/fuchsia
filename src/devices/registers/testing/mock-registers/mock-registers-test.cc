// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-registers.h"

#include <lib/async-loop/cpp/loop.h>

#include <zxtest/zxtest.h>

namespace mock_registers {

class MockRegistersTest : public zxtest::Test {
 public:
  void SetUp() override {
    zx_status_t status;

    loop_.StartThread();

    device_ = std::make_unique<MockRegistersDevice>(loop_.dispatcher());

    zx::channel client_end, server_end;
    if ((status = zx::channel::create(0, &client_end, &server_end)) != ZX_OK) {
      printf("Could not create channel %d\n", status);
      return;
    }
    device_->RegistersConnect(std::move(server_end));
    client_ = fidl::WireSyncClient<fuchsia_hardware_registers::Device>(std::move(client_end));
  }

  void TearDown() override {
    EXPECT_OK(registers()->VerifyAll());
    loop_.Shutdown();
  }

  MockRegisters* registers() { return device_->fidl_service(); }

 protected:
  std::unique_ptr<MockRegistersDevice> device_;

  fidl::WireSyncClient<fuchsia_hardware_registers::Device> client_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

TEST_F(MockRegistersTest, ReadTest) {
  // 8-bit
  registers()->ExpectRead<uint8_t>(0, 1, 2);
  auto read8_res = client_->ReadRegister8(0, 1);
  EXPECT_TRUE(read8_res.ok());
  EXPECT_TRUE(read8_res->result.is_response());
  EXPECT_EQ(read8_res->result.response().value, 2);

  // 16-bit
  registers()->ExpectRead<uint16_t>(5, 15, 3);
  auto read16_res = client_->ReadRegister16(5, 15);
  EXPECT_TRUE(read16_res.ok());
  EXPECT_TRUE(read16_res->result.is_response());
  EXPECT_EQ(read16_res->result.response().value, 3);

  // 32-bit
  registers()->ExpectRead<uint32_t>(145, 127, 25);
  auto read32_res = client_->ReadRegister32(145, 127);
  EXPECT_TRUE(read32_res.ok());
  EXPECT_TRUE(read32_res->result.is_response());
  EXPECT_EQ(read32_res->result.response().value, 25);

  // 64-bit
  registers()->ExpectRead<uint64_t>(325, 54, 136);
  auto read64_res = client_->ReadRegister64(325, 54);
  EXPECT_TRUE(read64_res.ok());
  EXPECT_TRUE(read64_res->result.is_response());
  EXPECT_EQ(read64_res->result.response().value, 136);

  // Multiple Reads
  registers()->ExpectRead<uint32_t>(25, 63, 46);
  registers()->ExpectRead<uint32_t>(25, 84, 53);
  registers()->ExpectRead<uint32_t>(102, 57, 7);
  registers()->ExpectRead<uint32_t>(3, 24, 299);
  registers()->ExpectRead<uint32_t>(102, 67, 38);
  auto res1 = client_->ReadRegister32(25, 63);
  EXPECT_TRUE(res1.ok());
  EXPECT_TRUE(res1->result.is_response());
  EXPECT_EQ(res1->result.response().value, 46);
  auto res2 = client_->ReadRegister32(25, 84);
  EXPECT_TRUE(res2.ok());
  EXPECT_TRUE(res2->result.is_response());
  EXPECT_EQ(res2->result.response().value, 53);
  auto res3 = client_->ReadRegister32(102, 57);
  EXPECT_TRUE(res3.ok());
  EXPECT_TRUE(res3->result.is_response());
  EXPECT_EQ(res3->result.response().value, 7);
  auto res4 = client_->ReadRegister32(3, 24);
  EXPECT_TRUE(res4.ok());
  EXPECT_TRUE(res4->result.is_response());
  EXPECT_EQ(res4->result.response().value, 299);
  auto res5 = client_->ReadRegister32(102, 67);
  EXPECT_TRUE(res5.ok());
  EXPECT_TRUE(res5->result.is_response());
  EXPECT_EQ(res5->result.response().value, 38);
}

TEST_F(MockRegistersTest, WriteTest) {
  // 8-bit
  registers()->ExpectWrite<uint8_t>(0, 1, 2);
  auto write8_res = client_->WriteRegister8(0, 1, 2);
  EXPECT_TRUE(write8_res.ok());
  EXPECT_TRUE(write8_res->result.is_response());

  // 16-bit
  registers()->ExpectWrite<uint16_t>(5, 15, 3);
  auto write16_res = client_->WriteRegister16(5, 15, 3);
  EXPECT_TRUE(write16_res.ok());
  EXPECT_TRUE(write16_res->result.is_response());

  // 32-bit
  registers()->ExpectWrite<uint32_t>(145, 127, 25);
  auto write32_res = client_->WriteRegister32(145, 127, 25);
  EXPECT_TRUE(write32_res.ok());
  EXPECT_TRUE(write32_res->result.is_response());

  // 64-bit
  registers()->ExpectWrite<uint64_t>(325, 54, 136);
  auto write64_res = client_->WriteRegister64(325, 54, 136);
  EXPECT_TRUE(write64_res.ok());
  EXPECT_TRUE(write64_res->result.is_response());

  // Multiple Writes
  registers()->ExpectWrite<uint32_t>(25, 63, 46);
  registers()->ExpectWrite<uint32_t>(25, 84, 53);
  registers()->ExpectWrite<uint32_t>(102, 57, 7);
  registers()->ExpectWrite<uint32_t>(3, 24, 299);
  registers()->ExpectWrite<uint32_t>(102, 67, 38);
  auto res1 = client_->WriteRegister32(25, 63, 46);
  EXPECT_TRUE(res1.ok());
  EXPECT_TRUE(res1->result.is_response());
  auto res2 = client_->WriteRegister32(25, 84, 53);
  EXPECT_TRUE(res2.ok());
  EXPECT_TRUE(res2->result.is_response());
  auto res3 = client_->WriteRegister32(102, 57, 7);
  EXPECT_TRUE(res3.ok());
  EXPECT_TRUE(res3->result.is_response());
  auto res4 = client_->WriteRegister32(3, 24, 299);
  EXPECT_TRUE(res4.ok());
  EXPECT_TRUE(res4->result.is_response());
  auto res5 = client_->WriteRegister32(102, 67, 38);
  EXPECT_TRUE(res5.ok());
  EXPECT_TRUE(res5->result.is_response());
}

}  // namespace mock_registers
