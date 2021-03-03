// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi.h"

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/spi/spi.h>

#include <map>

#include <ddk/metadata.h>
#include <ddk/metadata/spi.h>
#include <zxtest/zxtest.h>

namespace spi {

class FakeDdkSpiImpl;

class FakeDdkSpiImpl : public fake_ddk::Bind,
                       public ddk::SpiImplProtocol<FakeDdkSpiImpl, ddk::base_protocol> {
 public:
  explicit FakeDdkSpiImpl() {
    fake_ddk::Protocol proto = {&spi_impl_protocol_ops_, this};
    SetProtocol(ZX_PROTOCOL_SPI_IMPL, &proto);
    SetMetadata(DEVICE_METADATA_SPI_CHANNELS, &kSpiChannels, sizeof(kSpiChannels));
    SetMetadata(DEVICE_METADATA_PRIVATE, &kTestBusId, sizeof(kTestBusId));
  }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) {
    if (parent == fake_ddk::kFakeParent) {
      bus_device_ = reinterpret_cast<SpiDevice*>(args->ctx);
    } else if (parent == reinterpret_cast<zx_device_t*>(bus_device_)) {
      children_.push_back(reinterpret_cast<SpiChild*>(args->ctx));

      auto fidl = new fake_ddk::FidlMessenger;
      const zx_protocol_device_t* ops = reinterpret_cast<const zx_protocol_device_t*>(args->ops);
      fidl->SetMessageOp(args->ctx, ops->message);
      fidl_clients_.push_back(fidl);
    }
    *out = reinterpret_cast<zx_device_t*>(args->ctx);
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) {
    if (device == reinterpret_cast<zx_device_t*>(bus_device_)) {
      delete bus_device_;
      bus_device_ = nullptr;

      // The SpiChild device's unbind hooks will be called after the
      // Spi device has replied to its unbind.
      // We need to make a copy before iterating, as the SpiChild will be
      // erased from this |children_| list when it replies to its unbind.
      fbl::Vector<SpiChild*> children_copy;
      for (size_t i = 0; i < children_.size(); i++) {
        children_copy.push_back(children_[i]);
      }
      for (size_t i = 0; i < children_copy.size(); i++) {
        zx_device_t* zxdev = reinterpret_cast<zx_device_t*>(children_copy[i]);
        children_copy[i]->DdkUnbind(ddk::UnbindTxn(zxdev));
      }
      return ZX_OK;
    } else {
      for (size_t i = 0; i < children_.size(); i++) {
        if (children_[i] == reinterpret_cast<SpiChild*>(device)) {
          children_.erase(i);
          delete fidl_clients_[i];
          fidl_clients_.erase(i);
          return ZX_OK;
        }
      }
    }
    return ZX_ERR_BAD_STATE;
  }

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

  SpiDevice* bus_device_;
  fbl::Vector<SpiChild*> children_;
  fbl::Vector<fake_ddk::FidlMessenger*> fidl_clients_;
  uint32_t current_test_cs_;
  bool corrupt_rx_actual_ = false;

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

TEST(SpiDevice, SpiTest) {
  FakeDdkSpiImpl ddk;

  // make it
  SpiDevice::Create(nullptr, fake_ddk::kFakeParent);
  EXPECT_EQ(ddk.children_.size(), std::size(ddk.kSpiChannels));

  // test it
  uint8_t txbuf[] = {0, 1, 2, 3, 4, 5, 6};
  uint8_t rxbuf[sizeof txbuf];

  for (size_t i = 0; i < ddk.children_.size(); i++) {
    ddk.current_test_cs_ = static_cast<uint32_t>(i);
    auto channel = ddk.fidl_clients_[i]->local().get();

    ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kTransmit;
    zx_status_t status = spilib_transmit(channel, txbuf, sizeof txbuf);
    EXPECT_OK(status, "");

    ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kReceive;
    status = spilib_receive(channel, rxbuf, sizeof rxbuf);
    EXPECT_OK(status, "");

    ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kExchange;
    status = spilib_exchange(channel, txbuf, rxbuf, sizeof txbuf);
    EXPECT_OK(status, "");
  }

  // clean it up
  zx_device_t* zxdev = reinterpret_cast<zx_device_t*>(ddk.bus_device_);
  ddk.bus_device_->DdkUnbind(ddk::UnbindTxn(zxdev));
  EXPECT_EQ(ddk.children_.size(), 0, "");
  EXPECT_EQ(ddk.bus_device_, nullptr, "");
}

TEST(SpiDevice, SpiFidlVmoTest) {
  using ::fuchsia_hardware_sharedmemory::wire::SharedVmoRight;

  constexpr uint8_t kTestData[] = {1, 2, 3, 4, 5, 6, 7};

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("spi-test-thread"));

  FakeDdkSpiImpl ddk;

  fidl::Client<::fuchsia_hardware_spi::Device> cs0_client, cs1_client;

  SpiDevice::Create(nullptr, fake_ddk::kFakeParent);
  EXPECT_EQ(ddk.children_.size(), std::size(ddk.kSpiChannels));

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ddk.children_[0]->SpiConnectServer(std::move(server));
    ASSERT_OK(cs0_client.Bind(std::move(client), loop.dispatcher()));
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ddk.children_[1]->SpiConnectServer(std::move(server));
    ASSERT_OK(cs1_client.Bind(std::move(client), loop.dispatcher()));
  }

  zx::vmo cs0_vmo, cs1_vmo;
  ASSERT_OK(zx::vmo::create(4096, 0, &cs0_vmo));
  ASSERT_OK(zx::vmo::create(4096, 0, &cs1_vmo));

  {
    ::fuchsia_mem::wire::Range vmo = {.offset = 0, .size = 4096};
    ASSERT_OK(cs0_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo.vmo));
    auto result = cs0_client->RegisterVmo_Sync(1, std::move(vmo),
                                               SharedVmoRight::READ | SharedVmoRight::WRITE);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result->result.is_response());
  }

  {
    ::fuchsia_mem::wire::Range vmo = {.offset = 0, .size = 4096};
    ASSERT_OK(cs1_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo.vmo));
    auto result = cs1_client->RegisterVmo_Sync(2, std::move(vmo),
                                               SharedVmoRight::READ | SharedVmoRight::WRITE);
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

  zx_device_t* zxdev = reinterpret_cast<zx_device_t*>(ddk.bus_device_);
  ddk.bus_device_->DdkUnbind(ddk::UnbindTxn(zxdev));
  EXPECT_EQ(ddk.children_.size(), 0);
  EXPECT_EQ(ddk.bus_device_, nullptr);
}

TEST(SpiDevice, SpiFidlVectorTest) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("spi-test-thread"));

  FakeDdkSpiImpl ddk;

  fidl::Client<::fuchsia_hardware_spi::Device> cs0_client, cs1_client;

  SpiDevice::Create(nullptr, fake_ddk::kFakeParent);
  EXPECT_EQ(ddk.children_.size(), std::size(ddk.kSpiChannels));

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ddk.children_[0]->SpiConnectServer(std::move(server));
    ASSERT_OK(cs0_client.Bind(std::move(client), loop.dispatcher()));
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ddk.children_[1]->SpiConnectServer(std::move(server));
    ASSERT_OK(cs1_client.Bind(std::move(client), loop.dispatcher()));
  }

  uint8_t test_data[] = {1, 2, 3, 4, 5, 6, 7};

  ddk.current_test_cs_ = 0;
  ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kTransmit;
  {
    fidl::VectorView tx_buffer(fidl::unowned_ptr(test_data), countof(test_data));
    auto result = cs0_client->TransmitVector_Sync(std::move(tx_buffer));
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }

  ddk.current_test_cs_ = 1;
  ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kReceive;
  {
    auto result = cs1_client->ReceiveVector_Sync(sizeof(test_data));
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    ASSERT_EQ(result->data.count(), countof(test_data));
    EXPECT_BYTES_EQ(result->data.data(), test_data, sizeof(test_data));
  }

  ddk.current_test_cs_ = 0;
  ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kExchange;
  {
    fidl::VectorView tx_buffer(fidl::unowned_ptr(test_data), countof(test_data));
    auto result = cs0_client->ExchangeVector_Sync(std::move(tx_buffer));
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    ASSERT_EQ(result->rxdata.count(), countof(test_data));
    EXPECT_BYTES_EQ(result->rxdata.data(), test_data, sizeof(test_data));
  }

  zx_device_t* zxdev = reinterpret_cast<zx_device_t*>(ddk.bus_device_);
  ddk.bus_device_->DdkUnbind(ddk::UnbindTxn(zxdev));
  EXPECT_EQ(ddk.children_.size(), 0);
  EXPECT_EQ(ddk.bus_device_, nullptr);
}

TEST(SpiDevice, SpiFidlVectorErrorTest) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("spi-test-thread"));

  FakeDdkSpiImpl ddk;

  fidl::Client<::fuchsia_hardware_spi::Device> cs0_client, cs1_client;

  SpiDevice::Create(nullptr, fake_ddk::kFakeParent);
  EXPECT_EQ(ddk.children_.size(), std::size(ddk.kSpiChannels));

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ddk.children_[0]->SpiConnectServer(std::move(server));
    ASSERT_OK(cs0_client.Bind(std::move(client), loop.dispatcher()));
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ddk.children_[1]->SpiConnectServer(std::move(server));
    ASSERT_OK(cs1_client.Bind(std::move(client), loop.dispatcher()));
  }

  ddk.corrupt_rx_actual_ = true;

  uint8_t test_data[] = {1, 2, 3, 4, 5, 6, 7};

  ddk.current_test_cs_ = 0;
  ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kTransmit;
  {
    fidl::VectorView tx_buffer(fidl::unowned_ptr(test_data), countof(test_data));
    auto result = cs0_client->TransmitVector_Sync(std::move(tx_buffer));
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }

  ddk.current_test_cs_ = 1;
  ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kReceive;
  {
    auto result = cs1_client->ReceiveVector_Sync(sizeof(test_data));
    ASSERT_OK(result.status());
    EXPECT_EQ(result->status, ZX_ERR_INTERNAL);
    EXPECT_EQ(result->data.count(), 0);
  }

  ddk.current_test_cs_ = 0;
  ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kExchange;
  {
    fidl::VectorView tx_buffer(fidl::unowned_ptr(test_data), countof(test_data));
    auto result = cs0_client->ExchangeVector_Sync(std::move(tx_buffer));
    ASSERT_OK(result.status());
    EXPECT_EQ(result->status, ZX_ERR_INTERNAL);
    EXPECT_EQ(result->rxdata.count(), 0);
  }

  zx_device_t* zxdev = reinterpret_cast<zx_device_t*>(ddk.bus_device_);
  ddk.bus_device_->DdkUnbind(ddk::UnbindTxn(zxdev));
  EXPECT_EQ(ddk.children_.size(), 0);
  EXPECT_EQ(ddk.bus_device_, nullptr);
}

}  // namespace spi
