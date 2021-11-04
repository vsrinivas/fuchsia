// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi.h"

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/spi/spi.h>

#include <map>

#include <zxtest/zxtest.h>

#include "src/devices/lib/fidl-metadata/spi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace spi {
using spi_channel_t = fidl_metadata::spi::Channel;

class FakeDdkSpiImpl;

class FakeDdkSpiImpl : public ddk::SpiImplProtocol<FakeDdkSpiImpl, ddk::base_protocol> {
 public:
  spi_impl_protocol_ops_t* ops() { return &spi_impl_protocol_ops_; }

  uint32_t SpiImplGetChipSelectCount() { return 2; }

  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual) {
    EXPECT_EQ(cs, current_test_cs_, "");

    switch (test_mode_) {
      case SpiTestMode::kTransmit:
        EXPECT_NE(txdata, nullptr, "");
        EXPECT_NE(txdata_size, 0, "");
        EXPECT_EQ(out_rxdata, nullptr, "");
        EXPECT_EQ(rxdata_size, 0, "");
        *out_rxdata_actual = 0;
        break;
      case SpiTestMode::kReceive:
        EXPECT_EQ(txdata, nullptr, "");
        EXPECT_EQ(txdata_size, 0, "");
        EXPECT_NE(out_rxdata, nullptr, "");
        EXPECT_NE(rxdata_size, 0, "");
        memset(out_rxdata, 0, rxdata_size);
        memcpy(out_rxdata, kTestData, std::min(rxdata_size, sizeof(kTestData)));
        *out_rxdata_actual = rxdata_size + (corrupt_rx_actual_ ? 1 : 0);
        break;
      case SpiTestMode::kExchange:
        EXPECT_NE(txdata, nullptr, "");
        EXPECT_NE(txdata_size, 0, "");
        EXPECT_NE(out_rxdata, nullptr, "");
        EXPECT_NE(rxdata_size, 0, "");
        EXPECT_EQ(txdata_size, rxdata_size, "");
        memset(out_rxdata, 0, rxdata_size);
        memcpy(out_rxdata, txdata, std::min(rxdata_size, txdata_size));
        *out_rxdata_actual = std::min(rxdata_size, txdata_size) + (corrupt_rx_actual_ ? 1 : 0);
        break;
    }

    return ZX_OK;
  }

  zx_status_t SpiImplRegisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo vmo,
                                 uint64_t offset, uint64_t size, uint32_t rights) {
    if (chip_select > 1) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    std::map<uint32_t, zx::vmo>& map = chip_select == 0 ? cs0_vmos : cs1_vmos;
    if (map.find(vmo_id) != map.end()) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    map[vmo_id] = std::move(vmo);
    return ZX_OK;
  }

  zx_status_t SpiImplUnregisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo* out_vmo) {
    if (chip_select > 1) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    std::map<uint32_t, zx::vmo>& map = chip_select == 0 ? cs0_vmos : cs1_vmos;
    auto it = map.find(vmo_id);
    if (it == map.end()) {
      return ZX_ERR_NOT_FOUND;
    }

    if (out_vmo) {
      out_vmo->reset(std::get<1>(*it).release());
    }

    map.erase(it);
    return ZX_OK;
  }

  void SpiImplReleaseRegisteredVmos(uint32_t chip_select) { vmos_released_since_last_call_ = true; }

  zx_status_t SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                 uint64_t size) {
    if (chip_select > 1) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    std::map<uint32_t, zx::vmo>& map = chip_select == 0 ? cs0_vmos : cs1_vmos;
    auto it = map.find(vmo_id);
    if (it == map.end()) {
      return ZX_ERR_NOT_FOUND;
    }

    uint8_t buf[sizeof(kTestData)];
    zx_status_t status = std::get<1>(*it).read(buf, offset, std::max(size, sizeof(buf)));
    if (status != ZX_OK) {
      return status;
    }

    return memcmp(buf, kTestData, std::max(size, sizeof(buf))) == 0 ? ZX_OK : ZX_ERR_IO;
  }

  zx_status_t SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                uint64_t size) {
    if (chip_select > 1) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    std::map<uint32_t, zx::vmo>& map = chip_select == 0 ? cs0_vmos : cs1_vmos;
    auto it = map.find(vmo_id);
    if (it == map.end()) {
      return ZX_ERR_NOT_FOUND;
    }

    return std::get<1>(*it).write(kTestData, offset, std::max(size, sizeof(kTestData)));
  }

  zx_status_t SpiImplExchangeVmo(uint32_t chip_select, uint32_t tx_vmo_id, uint64_t tx_offset,
                                 uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size) {
    if (chip_select > 1) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    std::map<uint32_t, zx::vmo>& map = chip_select == 0 ? cs0_vmos : cs1_vmos;
    auto tx_it = map.find(tx_vmo_id);
    auto rx_it = map.find(rx_vmo_id);

    if (tx_it == map.end() || rx_it == map.end()) {
      return ZX_ERR_NOT_FOUND;
    }

    uint8_t buf[8];
    zx_status_t status = std::get<1>(*tx_it).read(buf, tx_offset, std::max(size, sizeof(buf)));
    if (status != ZX_OK) {
      return status;
    }

    return std::get<1>(*rx_it).write(buf, rx_offset, std::max(size, sizeof(buf)));
  }

  bool vmos_released_since_last_call() {
    const bool value = vmos_released_since_last_call_;
    vmos_released_since_last_call_ = false;
    return value;
  }

  zx_status_t SpiImplLockBus(uint32_t chip_select) { return ZX_OK; }
  zx_status_t SpiImplUnlockBus(uint32_t chip_select) { return ZX_OK; }

  SpiDevice* bus_device_;
  uint32_t current_test_cs_ = 0;
  bool corrupt_rx_actual_ = false;
  bool vmos_released_since_last_call_ = false;

  enum class SpiTestMode {
    kTransmit,
    kReceive,
    kExchange,
  } test_mode_;

  static constexpr uint32_t kTestBusId = 0;
  static constexpr spi_channel_t kSpiChannels[] = {
      {.bus_id = 0, .cs = 0, .vid = 0, .pid = 0, .did = 0},
      {.bus_id = 0, .cs = 1, .vid = 0, .pid = 0, .did = 0}};

  std::map<uint32_t, zx::vmo> cs0_vmos;
  std::map<uint32_t, zx::vmo> cs1_vmos;

 private:
  static constexpr uint8_t kTestData[] = {1, 2, 3, 4, 5, 6, 7};
};

class SpiDeviceTest : public zxtest::Test {
 public:
  SpiDeviceTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

  void SetUp() override {
    parent_ = MockDevice::FakeRootParent();
    ASSERT_OK(loop_.StartThread("spi-test-thread"));

    parent_->AddProtocol(ZX_PROTOCOL_SPI_IMPL, spi_impl_.ops(), &spi_impl_);

    SetSpiChannelMetadata(kSpiChannels, countof(kSpiChannels));
    parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kTestBusId, sizeof(kTestBusId));
  }

 protected:
  static constexpr uint32_t kTestBusId = 0;
  static constexpr spi_channel_t kSpiChannels[] = {
      {.bus_id = 0, .cs = 0, .vid = 0, .pid = 0, .did = 0},
      {.bus_id = 0, .cs = 1, .vid = 0, .pid = 0, .did = 0},
  };

  void SetSpiChannelMetadata(const spi_channel_t* channels, size_t count) {
    const auto result =
        fidl_metadata::spi::SpiChannelsToFidl(cpp20::span<const spi_channel_t>(channels, count));
    ASSERT_OK(result.status_value());
    parent_->SetMetadata(DEVICE_METADATA_SPI_CHANNELS, result->data(), result->size());
  }

  std::shared_ptr<MockDevice> parent_;
  FakeDdkSpiImpl spi_impl_;
  async::Loop loop_;
};

TEST_F(SpiDeviceTest, SpiTest) {
  // make it
  SpiDevice::Create(nullptr, parent_.get());
  auto* const spi_bus = parent_->GetLatestChild();
  ASSERT_NOT_NULL(spi_bus);
  EXPECT_EQ(spi_bus->child_count(), std::size(kSpiChannels));

  // test it
  uint8_t txbuf[] = {0, 1, 2, 3, 4, 5, 6};
  uint8_t rxbuf[sizeof txbuf];

  size_t i = 0;
  for (auto it = spi_bus->children().begin(); it != spi_bus->children().end(); it++, i++) {
    spi_impl_.current_test_cs_ = i;

    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    fidl::BindServer(loop_.dispatcher(), std::move(server), (*it)->GetDeviceContext<SpiChild>());

    spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kTransmit;
    zx_status_t status = spilib_transmit(client.get(), txbuf, sizeof txbuf);
    EXPECT_OK(status, "");

    spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kReceive;
    status = spilib_receive(client.get(), rxbuf, sizeof rxbuf);
    EXPECT_OK(status, "");

    spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kExchange;
    status = spilib_exchange(client.get(), txbuf, rxbuf, sizeof txbuf);
    EXPECT_OK(status, "");
  }

  // clean it up
  spi_bus->ReleaseOp();
  EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(parent_.get()));
  EXPECT_EQ(parent_->descendant_count(), 0);
}

TEST_F(SpiDeviceTest, SpiFidlVmoTest) {
  using fuchsia_hardware_sharedmemory::wire::SharedVmoRight;

  constexpr uint8_t kTestData[] = {1, 2, 3, 4, 5, 6, 7};

  SpiDevice::Create(nullptr, parent_.get());
  auto* const spi_bus = parent_->GetLatestChild();
  ASSERT_NOT_NULL(spi_bus);
  EXPECT_EQ(spi_bus->child_count(), std::size(kSpiChannels));

  fidl::WireSharedClient<fuchsia_hardware_spi::Device> cs0_client, cs1_client;

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child0 = spi_bus->children().front();
    child0->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs0_client.Bind(std::move(client), loop_.dispatcher());
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child1 = *++spi_bus->children().begin();
    child1->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs1_client.Bind(std::move(client), loop_.dispatcher());
  }

  zx::vmo cs0_vmo, cs1_vmo;
  ASSERT_OK(zx::vmo::create(4096, 0, &cs0_vmo));
  ASSERT_OK(zx::vmo::create(4096, 0, &cs1_vmo));

  {
    fuchsia_mem::wire::Range vmo = {.offset = 0, .size = 4096};
    ASSERT_OK(cs0_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo.vmo));
    auto result = cs0_client->RegisterVmo_Sync(1, std::move(vmo),
                                               SharedVmoRight::kRead | SharedVmoRight::kWrite);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result->result.is_response());
  }

  {
    fuchsia_mem::wire::Range vmo = {.offset = 0, .size = 4096};
    ASSERT_OK(cs1_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo.vmo));
    auto result = cs1_client->RegisterVmo_Sync(2, std::move(vmo),
                                               SharedVmoRight::kRead | SharedVmoRight::kWrite);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result->result.is_response());
  }

  ASSERT_OK(cs0_vmo.write(kTestData, 1024, sizeof(kTestData)));
  {
    auto result = cs0_client->Exchange_Sync(
        {
            .vmo_id = 1,
            .offset = 1024,
            .size = sizeof(kTestData),
        },
        {
            .vmo_id = 1,
            .offset = 2048,
            .size = sizeof(kTestData),
        });
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result->result.is_response());

    uint8_t buf[sizeof(kTestData)];
    ASSERT_OK(cs0_vmo.read(buf, 2048, sizeof(buf)));
    EXPECT_BYTES_EQ(buf, kTestData, sizeof(buf));
  }

  ASSERT_OK(cs1_vmo.write(kTestData, 1024, sizeof(kTestData)));
  {
    auto result = cs1_client->Transmit_Sync({
        .vmo_id = 2,
        .offset = 1024,
        .size = sizeof(kTestData),
    });
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result->result.is_response());
  }

  {
    auto result = cs0_client->Receive_Sync({
        .vmo_id = 1,
        .offset = 1024,
        .size = sizeof(kTestData),
    });
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result->result.is_response());

    uint8_t buf[sizeof(kTestData)];
    ASSERT_OK(cs0_vmo.read(buf, 1024, sizeof(buf)));
    EXPECT_BYTES_EQ(buf, kTestData, sizeof(buf));
  }

  {
    auto result = cs0_client->UnregisterVmo_Sync(1);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result->result.is_response());
  }

  {
    auto result = cs1_client->UnregisterVmo_Sync(2);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result->result.is_response());
  }

  spi_bus->ReleaseOp();
  EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(parent_.get()));
  EXPECT_EQ(parent_->descendant_count(), 0);
}

TEST_F(SpiDeviceTest, SpiFidlVectorTest) {
  fidl::WireSharedClient<fuchsia_hardware_spi::Device> cs0_client, cs1_client;

  SpiDevice::Create(nullptr, parent_.get());
  auto* const spi_bus = parent_->GetLatestChild();
  ASSERT_NOT_NULL(spi_bus);
  EXPECT_EQ(spi_bus->child_count(), std::size(kSpiChannels));

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child0 = spi_bus->children().front();
    child0->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs0_client.Bind(std::move(client), loop_.dispatcher());
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child1 = *++spi_bus->children().begin();
    child1->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs1_client.Bind(std::move(client), loop_.dispatcher());
  }

  uint8_t test_data[] = {1, 2, 3, 4, 5, 6, 7};

  spi_impl_.current_test_cs_ = 0;
  spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kTransmit;
  {
    auto tx_buffer = fidl::VectorView<uint8_t>::FromExternal(test_data);
    auto result = cs0_client->TransmitVector_Sync(tx_buffer);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }

  spi_impl_.current_test_cs_ = 1;
  spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kReceive;
  {
    auto result = cs1_client->ReceiveVector_Sync(sizeof(test_data));
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    ASSERT_EQ(result->data.count(), countof(test_data));
    EXPECT_BYTES_EQ(result->data.data(), test_data, sizeof(test_data));
  }

  spi_impl_.current_test_cs_ = 0;
  spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kExchange;
  {
    auto tx_buffer = fidl::VectorView<uint8_t>::FromExternal(test_data);
    auto result = cs0_client->ExchangeVector_Sync(tx_buffer);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    ASSERT_EQ(result->rxdata.count(), countof(test_data));
    EXPECT_BYTES_EQ(result->rxdata.data(), test_data, sizeof(test_data));
  }

  spi_bus->ReleaseOp();
  EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(parent_.get()));
  EXPECT_EQ(parent_->descendant_count(), 0);
}

TEST_F(SpiDeviceTest, SpiFidlVectorErrorTest) {
  fidl::WireSharedClient<fuchsia_hardware_spi::Device> cs0_client, cs1_client;

  SpiDevice::Create(nullptr, parent_.get());
  auto* const spi_bus = parent_->GetLatestChild();
  ASSERT_NOT_NULL(spi_bus);
  EXPECT_EQ(spi_bus->child_count(), std::size(kSpiChannels));

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child0 = spi_bus->children().front();
    child0->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs0_client.Bind(std::move(client), loop_.dispatcher());
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child1 = *++spi_bus->children().begin();
    child1->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs1_client.Bind(std::move(client), loop_.dispatcher());
  }

  spi_impl_.corrupt_rx_actual_ = true;

  uint8_t test_data[] = {1, 2, 3, 4, 5, 6, 7};

  spi_impl_.current_test_cs_ = 0;
  spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kTransmit;
  {
    auto tx_buffer = fidl::VectorView<uint8_t>::FromExternal(test_data);
    auto result = cs0_client->TransmitVector_Sync(tx_buffer);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }

  spi_impl_.current_test_cs_ = 1;
  spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kReceive;
  {
    auto result = cs1_client->ReceiveVector_Sync(sizeof(test_data));
    ASSERT_OK(result.status());
    EXPECT_EQ(result->status, ZX_ERR_INTERNAL);
    EXPECT_EQ(result->data.count(), 0);
  }

  spi_impl_.current_test_cs_ = 0;
  spi_impl_.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kExchange;
  {
    auto tx_buffer = fidl::VectorView<uint8_t>::FromExternal(test_data);
    auto result = cs0_client->ExchangeVector_Sync(tx_buffer);
    ASSERT_OK(result.status());
    EXPECT_EQ(result->status, ZX_ERR_INTERNAL);
    EXPECT_EQ(result->rxdata.count(), 0);
  }

  spi_bus->ReleaseOp();
  EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(parent_.get()));
  EXPECT_EQ(parent_->descendant_count(), 0);
}

TEST_F(SpiDeviceTest, AssertCsWithSiblingTest) {
  fidl::WireSharedClient<fuchsia_hardware_spi::Device> cs0_client, cs1_client;

  SpiDevice::Create(nullptr, parent_.get());
  auto* const spi_bus = parent_->GetLatestChild();
  ASSERT_NOT_NULL(spi_bus);
  EXPECT_EQ(spi_bus->child_count(), std::size(kSpiChannels));

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child0 = spi_bus->children().front();
    child0->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs0_client.Bind(std::move(client), loop_.dispatcher());
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child1 = *++spi_bus->children().begin();
    child1->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs1_client.Bind(std::move(client), loop_.dispatcher());
  }

  {
    auto result = cs0_client->CanAssertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_FALSE(result->can);
  }

  {
    auto result = cs1_client->CanAssertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_FALSE(result->can);
  }

  {
    auto result = cs0_client->AssertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
  }

  {
    auto result = cs1_client->AssertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
  }

  {
    auto result = cs0_client->DeassertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
  }

  {
    auto result = cs1_client->DeassertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
  }
}

TEST_F(SpiDeviceTest, AssertCsNoSiblingTest) {
  SetSpiChannelMetadata(kSpiChannels, 1);

  fidl::WireSharedClient<fuchsia_hardware_spi::Device> cs0_client;

  SpiDevice::Create(nullptr, parent_.get());
  auto* const spi_bus = parent_->GetLatestChild();
  ASSERT_NOT_NULL(spi_bus);
  EXPECT_EQ(spi_bus->child_count(), 1);

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    const auto& child0 = spi_bus->children().front();
    child0->GetDeviceContext<SpiChild>()->SpiConnectServer(std::move(server));
    cs0_client.Bind(std::move(client), loop_.dispatcher());
  }

  {
    auto result = cs0_client->CanAssertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->can);
  }

  {
    auto result = cs0_client->AssertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result->status);
  }

  {
    auto result = cs0_client->DeassertCs_Sync();
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result->status);
  }
}

TEST_F(SpiDeviceTest, OneClient) {
  SetSpiChannelMetadata(kSpiChannels, 1);

  fidl::WireSyncClient<fuchsia_hardware_spi::Device> cs0_client;

  SpiDevice::Create(nullptr, parent_.get());
  auto* const spi_bus = parent_->GetLatestChild();
  ASSERT_NOT_NULL(spi_bus);
  EXPECT_EQ(spi_bus->child_count(), 1);

  auto* const child0 = spi_bus->children().front()->GetDeviceContext<SpiChild>();

  // Establish a FIDL connection and verify that it works.
  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    child0->SpiConnectServer(std::move(server));
    cs0_client = fidl::WireSyncClient<fuchsia_hardware_spi::Device>(std::move(client));
  }

  {
    auto result = cs0_client->CanAssertCs();
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->can);
  }

  // Trying to make a new connection should fail.
  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    child0->SpiConnectServer(std::move(server));
    fidl::WireSyncClient<fuchsia_hardware_spi::Device> cs0_client_1(std::move(client));

    auto result = cs0_client_1->CanAssertCs();
    EXPECT_FALSE(result.ok());
  }

  EXPECT_FALSE(spi_impl_.vmos_released_since_last_call());

  // Close the first client so that another one can connect.
  cs0_client = {};

  // We don't know when the driver will be ready for a new client, just loop
  // until the connection is established.
  for (;;) {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    child0->SpiConnectServer(std::move(server));
    cs0_client = fidl::WireSyncClient<fuchsia_hardware_spi::Device>(std::move(client));

    auto result = cs0_client->CanAssertCs();
    if (result.ok()) {
      break;
    }
  }

  EXPECT_TRUE(spi_impl_.vmos_released_since_last_call());

  // DdkOpen should fail when another client is connected
  EXPECT_NOT_OK(child0->DdkOpen(nullptr, 0));

  // Close the first client and make sure DdkOpen now works
  cs0_client = {};

  while (child0->DdkOpen(nullptr, 0) != ZX_OK) {
  }

  EXPECT_TRUE(spi_impl_.vmos_released_since_last_call());

  // FIDL clients shouldn't be able to connect, and calling DdkOpen a second
  // time should fail
  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    child0->SpiConnectServer(std::move(server));

    fidl::WireSyncClient<fuchsia_hardware_spi::Device> cs0_client_1(std::move(client));

    auto result = cs0_client_1->CanAssertCs();
    EXPECT_FALSE(result.ok());
  }

  EXPECT_NOT_OK(child0->DdkOpen(nullptr, 0));

  // Call DdkClose and make sure that a new client can now connect.
  child0->DdkClose(0);

  for (;;) {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    child0->SpiConnectServer(std::move(server));

    cs0_client = fidl::WireSyncClient<fuchsia_hardware_spi::Device>(std::move(client));

    auto result = cs0_client->CanAssertCs();
    if (result.ok()) {
      break;
    }
  }

  EXPECT_TRUE(spi_impl_.vmos_released_since_last_call());
}

TEST_F(SpiDeviceTest, DdkLifecycle) {
  SetSpiChannelMetadata(kSpiChannels, 1);

  fidl::WireSyncClient<fuchsia_hardware_spi::Device> cs0_client;

  SpiDevice::Create(nullptr, parent_.get());
  auto* const spi_bus = parent_->GetLatestChild();
  ASSERT_NOT_NULL(spi_bus);
  EXPECT_EQ(spi_bus->child_count(), 1);

  auto* const child0 = spi_bus->children().front()->GetDeviceContext<SpiChild>();

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    child0->SpiConnectServer(std::move(server));
    cs0_client = fidl::WireSyncClient<fuchsia_hardware_spi::Device>(std::move(client));
  }

  {
    auto result = cs0_client->AssertCs();
    ASSERT_TRUE(result.ok());
    EXPECT_OK(result->status);
  }

  spi_bus->children().front()->UnbindOp();
  EXPECT_TRUE(spi_bus->children().front()->UnbindReplyCalled());

  {
    auto result = cs0_client->DeassertCs();
    ASSERT_TRUE(result.ok());
    // DdkUnbind has been called, the child device should respond with errors.
    EXPECT_NOT_OK(result->status);
  }

  spi_bus->children().front()->ReleaseOp();

  EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(parent_.get()));
  EXPECT_EQ(spi_bus->descendant_count(), 0);

  {
    auto result = cs0_client->DeassertCs();
    ASSERT_TRUE(result.ok());
    // The child should still exist and reply since the parent holds a reference to it.
    EXPECT_NOT_OK(result->status);
  }

  spi_bus->UnbindOp();
  EXPECT_TRUE(spi_bus->UnbindReplyCalled());

  {
    auto result = cs0_client->DeassertCs();
    // The parent has stopped its loop, this should now fail.
    EXPECT_FALSE(result.ok());
  }

  spi_bus->ReleaseOp();
  EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(parent_.get()));
  EXPECT_EQ(parent_->descendant_count(), 0);
}

}  // namespace spi
