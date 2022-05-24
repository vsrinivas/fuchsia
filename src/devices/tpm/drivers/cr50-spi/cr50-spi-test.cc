// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tpm/drivers/cr50-spi/cr50-spi.h"

#include <fidl/fuchsia.hardware.spi/cpp/wire.h>
#include <fidl/fuchsia.hardware.tpmimpl/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/debug.h>
#include <lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zircon/rights.h>

#include <deque>

#include <zxtest/zxtest.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

struct SpiMessage {
  SpiMessage(std::vector<uint8_t> t, std::vector<uint8_t> r) : tx(std::move(t)), rx(std::move(r)) {}
  // What we expect the driver to tx.
  std::vector<uint8_t> tx;
  // What we reply to the driver.
  std::vector<uint8_t> rx;
};

class Cr50SpiTest : public zxtest::Test,
                    public fidl::WireServer<fuchsia_hardware_spi::Device>,
                    public inspect::InspectTestHelper {
 public:
  Cr50SpiTest()
      : loop_(&kAsyncLoopConfigNeverAttachToThread), fake_root_(MockDevice::FakeRootParent()) {}
  void SetUp() override { ASSERT_OK(loop_.StartThread("async-loop-thread")); }

  void CreateDevice(bool with_interrupt) {
    if (with_interrupt) {
      zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &ready_irq_);
      fake_acpi_.SetMapInterrupt(
          [this](acpi::mock::Device::MapInterruptRequestView req,
                 acpi::mock::Device::MapInterruptCompleter::Sync& completer) {
            ASSERT_EQ(req->index, 0);
            zx::interrupt dupe;
            ready_irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dupe);
            completer.ReplySuccess(std::move(dupe));
          });
    }

    auto acpi = fake_acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(acpi.status_value());
    auto spi = MakeSpiClient();

    auto device = std::make_unique<cr50::spi::Cr50SpiDevice>(
        fake_root_.get(), std::move(acpi.value()), std::move(spi));
    ASSERT_OK(device->Bind(&device));
  }

  fidl::WireSyncClient<fuchsia_hardware_spi::Device> MakeSpiClient() {
    fidl::ServerEnd<fuchsia_hardware_spi::Device> server;
    auto client = fidl::CreateEndpoints(&server);
    ZX_ASSERT(client.is_ok());
    fidl::BindServer(loop_.dispatcher(), std::move(server), this);
    return fidl::BindSyncClient(std::move(*client));
  }

  void Exchange(fidl::VectorView<uint8_t> transmit, fidl::VectorView<uint8_t>* receive) {
    SpiMessage next = messages_.front();
    ASSERT_EQ(transmit.count(), next.tx.size());
    ASSERT_BYTES_EQ(transmit.data(), next.tx.data(), next.tx.size());
    if (receive) {
      ASSERT_EQ(receive->count(), next.rx.size());
      memcpy(receive->mutable_data(), next.rx.data(), next.rx.size());
    } else {
      ASSERT_EQ(next.rx.size(), 0);
    }
    messages_.pop_front();
  }

  void TransmitVector(TransmitVectorRequestView request,
                      TransmitVectorCompleter::Sync& completer) override {
    ASSERT_NO_FATAL_FAILURE(Exchange(request->data, nullptr));
    completer.Reply(ZX_OK);
  }
  void ReceiveVector(ReceiveVectorRequestView request,
                     ReceiveVectorCompleter::Sync& completer) override {
    fidl::Arena<> alloc;
    fidl::VectorView<uint8_t> out(alloc, request->size);
    ASSERT_NO_FATAL_FAILURE(Exchange(fidl::VectorView<uint8_t>(), &out));
    completer.Reply(ZX_OK, out);
  }
  void ExchangeVector(ExchangeVectorRequestView request,
                      ExchangeVectorCompleter::Sync& completer) override {
    fidl::Arena<> alloc;
    fidl::VectorView<uint8_t> out(alloc, request->txdata.count());
    ASSERT_NO_FATAL_FAILURE(Exchange(request->txdata, &out));
    completer.Reply(ZX_OK, out);
  }

  void RegisterVmo(RegisterVmoRequestView request, RegisterVmoCompleter::Sync& completer) override {
    ASSERT_TRUE(false, "unsupported");
  }
  void UnregisterVmo(UnregisterVmoRequestView request,
                     UnregisterVmoCompleter::Sync& completer) override {
    ASSERT_TRUE(false, "unsupported");
  }

  void Transmit(TransmitRequestView request, TransmitCompleter::Sync& completer) override {
    ASSERT_TRUE(false, "unsupported");
  }
  void Receive(ReceiveRequestView request, ReceiveCompleter::Sync& completer) override {
    ASSERT_TRUE(false, "unsupported");
  }
  void Exchange(ExchangeRequestView request, ExchangeCompleter::Sync& completer) override {
    ASSERT_TRUE(false, "unsupported");
  }

  void CanAssertCs(CanAssertCsRequestView request, CanAssertCsCompleter::Sync& completer) override {
    completer.Reply(true);
  }

  void AssertCs(AssertCsRequestView request, AssertCsCompleter::Sync& completer) override {
    ASSERT_FALSE(cs_asserted_);
    cs_asserted_ = true;
    completer.Reply(ZX_OK);
  }

  void DeassertCs(DeassertCsRequestView request, DeassertCsCompleter::Sync& completer) override {
    ASSERT_TRUE(cs_asserted_);
    cs_asserted_ = false;
    completer.Reply(ZX_OK);
  }

  void ExpectMessage(bool writing, uint16_t address, std::vector<uint8_t> tx,
                     std::vector<uint8_t> rx, size_t flow_control = 0) {
    size_t len = tx.empty() ? rx.size() : tx.size();

    std::vector<uint8_t> empty;
    std::vector<uint8_t> header_tx(4);
    header_tx[0] = len - 1;
    if (!writing) {
      header_tx[0] |= 0x80;
    }
    header_tx[1] = 0xd4;
    header_tx[2] = (address >> 8) & 0xff;
    header_tx[3] = address & 0xff;

    messages_.emplace_back(SpiMessage(std::move(header_tx), std::vector<uint8_t>(4)));

    for (size_t i = 0; i < flow_control; i++) {
      messages_.emplace_back(SpiMessage(empty, std::vector<uint8_t>(1)));
    }

    std::vector<uint8_t> ready(1);
    ready[0] = 1;
    messages_.emplace_back(SpiMessage(empty, ready));

    messages_.emplace_back(SpiMessage(std::move(tx), std::move(rx)));
  }

  void ExpectFirmware(std::string firmware) {
    char firmware_version[96] = {0};
    ASSERT_LT(firmware.size() + 1, 96);
    memcpy(firmware_version, firmware.data(), firmware.size() + 1);

    ExpectMessage(true, 0xf90, {0}, {});

    for (size_t i = 0; i < firmware.size(); i += 32) {
      ExpectMessage(false, 0xf90, {},
                    std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&firmware_version[i]),
                                         reinterpret_cast<uint8_t*>(&firmware_version[i + 32])),
                    10);
    }

    auto device = fake_root_->GetLatestChild();
    device->InitOp();
    device->WaitUntilInitReplyCalled();
  }

 protected:
  async::Loop loop_;
  std::shared_ptr<MockDevice> fake_root_;
  acpi::mock::Device fake_acpi_;
  zx::interrupt ready_irq_;
  std::deque<SpiMessage> messages_;
  bool cs_asserted_ = false;
};

TEST_F(Cr50SpiTest, TestFirmwareVersion) {
  ASSERT_NO_FATAL_FAILURE(CreateDevice(false));
  static const std::string kFirmwareVersion =
      "B2-C:0 RO_B:0.0.11/4d655eab RW_B:0.5.9/cr50_v1.9308_87_mp.547-af2f3d63";
  ASSERT_NO_FATAL_FAILURE(ExpectFirmware(kFirmwareVersion));
  ASSERT_EQ(messages_.size(), 0);

  auto device = fake_root_->GetLatestChild();
  auto ctx = device->GetDeviceContext<cr50::spi::Cr50SpiDevice>();
  ASSERT_NO_FATAL_FAILURE(ReadInspect(ctx->inspect()));
  CheckProperty(hierarchy().node(), "fw-version", inspect::StringPropertyValue(kFirmwareVersion));
}

TEST_F(Cr50SpiTest, TestTpmRead) {
  ASSERT_NO_FATAL_FAILURE(CreateDevice(false));
  ASSERT_NO_FATAL_FAILURE(ExpectFirmware("hello firmware"));

  fidl::ClientEnd<fuchsia_hardware_tpmimpl::TpmImpl> client_end;
  auto server = fidl::CreateEndpoints(&client_end);
  ASSERT_TRUE(server.is_ok());
  fidl::WireSyncClient client{std::move(client_end)};

  ddk::TpmImplProtocolClient proto(fake_root_->GetLatestChild());
  proto.ConnectServer(server->TakeChannel());

  std::vector<uint8_t> expected{1, 2, 3, 4};
  ExpectMessage(false, fuchsia_hardware_tpmimpl::wire::RegisterAddress::kTpmSts, {}, expected);
  auto read = client->Read(0, fuchsia_hardware_tpmimpl::wire::RegisterAddress::kTpmSts, 4);
  ASSERT_TRUE(read.ok());
  ASSERT_TRUE(read.Unwrap_NEW()->is_ok());
  auto& view = read.Unwrap_NEW()->value()->data;
  ASSERT_EQ(view.count(), expected.size());
  ASSERT_BYTES_EQ(view.data(), expected.data(), expected.size());
  ASSERT_EQ(messages_.size(), 0);
}

TEST_F(Cr50SpiTest, TestTpmWrite) {
  ASSERT_NO_FATAL_FAILURE(CreateDevice(false));
  ASSERT_NO_FATAL_FAILURE(ExpectFirmware("hello firmware"));

  fidl::ClientEnd<fuchsia_hardware_tpmimpl::TpmImpl> client_end;
  auto server = fidl::CreateEndpoints(&client_end);
  ASSERT_TRUE(server.is_ok());
  fidl::WireSyncClient client{std::move(client_end)};

  ddk::TpmImplProtocolClient proto(fake_root_->GetLatestChild());
  proto.ConnectServer(server->TakeChannel());

  std::vector<uint8_t> expected{4, 4, 2, 0};
  ExpectMessage(true, fuchsia_hardware_tpmimpl::wire::RegisterAddress::kTpmSts, expected, {});
  auto read = client->Write(0, fuchsia_hardware_tpmimpl::wire::RegisterAddress::kTpmSts,
                            fidl::VectorView<uint8_t>::FromExternal(expected));
  ASSERT_TRUE(read.ok());
  ASSERT_TRUE(read.Unwrap_NEW()->is_ok());
  ASSERT_EQ(messages_.size(), 0);
}
