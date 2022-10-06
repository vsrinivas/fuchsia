// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.tpmimpl/cpp/wire.h>
#include <fuchsia/hardware/tpmimpl/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/devices/tpm/drivers/tpm-lpc/tpm-lpc.h"

class FakeMmio {
 public:
  FakeMmio() {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegArrayLength);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t),
                                                          kRegArrayLength);
  }

  fdf::MmioBuffer MmioBuffer() { return fdf::MmioBuffer(mmio_->GetMmioBuffer()); }

  ddk_fake::FakeMmioReg& FakeRegister(size_t address) { return regs_[address >> 2]; }

 private:
  static constexpr size_t kMmioBufferSize = 0x5000;
  static constexpr size_t kRegArrayLength = kMmioBufferSize / sizeof(uint32_t);
  std::unique_ptr<ddk_fake::FakeMmioReg[]> regs_;
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> mmio_;
};

class TpmLpcTest : public zxtest::Test {
 public:
  TpmLpcTest()
      : loop_(&kAsyncLoopConfigNeverAttachToThread), fake_root_(MockDevice::FakeRootParent()) {}
  using AcpiDevice = acpi::mock::Device;

  void SetUp() override { ASSERT_OK(loop_.StartThread("tpm-lpc-test-fidl")); }

  void CreateDevice() {
    auto acpi = fake_acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(acpi.status_value());
    auto device = std::make_unique<tpm::lpc::TpmLpc>(fake_root_.get(), std::move(acpi.value()),
                                                     fake_mmio_.MmioBuffer());
    ASSERT_OK(device->Bind(&device));
  }

 protected:
  async::Loop loop_;
  std::shared_ptr<MockDevice> fake_root_;
  acpi::mock::Device fake_acpi_;
  FakeMmio fake_mmio_;
};

TEST_F(TpmLpcTest, TestTpmRead) {
  ASSERT_NO_FATAL_FAILURE(CreateDevice());

  fidl::ClientEnd<fuchsia_hardware_tpmimpl::TpmImpl> client_end;
  auto server = fidl::CreateEndpoints(&client_end);
  ASSERT_TRUE(server.is_ok());
  fidl::WireSyncClient client{std::move(client_end)};

  ddk::TpmImplProtocolClient proto(fake_root_->GetLatestChild());
  proto.ConnectServer(server->TakeChannel());

  std::vector<uint8_t> expected{0xFF, 0xFF, 0xFF, 0xFF};

  auto& sts_reg = fake_mmio_.FakeRegister(
      static_cast<uint16_t>(fuchsia_hardware_tpmimpl::wire::RegisterAddress::kTpmSts));
  sts_reg.SetReadCallback([]() { return 0xFFFFFFFFFFFFFFFF; });
  auto read = client->Read(0, fuchsia_hardware_tpmimpl::wire::RegisterAddress::kTpmSts, 4);
  ASSERT_TRUE(read.ok());
  ASSERT_TRUE(read->is_ok());
  auto& view = read->value()->data;
  ASSERT_EQ(view.count(), expected.size());
  ASSERT_BYTES_EQ(view.data(), expected.data(), expected.size());
}

TEST_F(TpmLpcTest, TestTpmWrite) {
  ASSERT_NO_FATAL_FAILURE(CreateDevice());

  fidl::ClientEnd<fuchsia_hardware_tpmimpl::TpmImpl> client_end;
  auto server = fidl::CreateEndpoints(&client_end);
  ASSERT_TRUE(server.is_ok());
  fidl::WireSyncClient client{std::move(client_end)};

  ddk::TpmImplProtocolClient proto(fake_root_->GetLatestChild());
  proto.ConnectServer(server->TakeChannel());

  std::vector<uint8_t> expected{0xFF, 0xFF, 0xFF, 0xFF};
  auto write = client->Write(0, fuchsia_hardware_tpmimpl::wire::RegisterAddress::kTpmSts,
                             fidl::VectorView<uint8_t>::FromExternal(expected));
  ASSERT_TRUE(write.ok());
  ASSERT_TRUE(write->is_ok());
}
