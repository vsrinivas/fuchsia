// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <zircon/errors.h>

#include <algorithm>
#include <numeric>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

// An I2C device that requires retries.
class FlakyI2cDevice : public fake_i2c::FakeI2c {
 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    count_++;
    // Unique errors below to check for retries.
    switch (count_) {
      // clang-format off
      case 1: return ZX_ERR_INTERNAL; break;
      case 2: return ZX_ERR_NOT_SUPPORTED; break;
      case 3: return ZX_ERR_NO_RESOURCES; break;
      case 4: return ZX_ERR_NO_MEMORY; break;
      case 5: *read_buffer_size = 1; return ZX_OK; break;
      default: ZX_ASSERT(0);  // Anything else is an error.
        // clang-format on
    }
    return ZX_OK;
  }

 private:
  size_t count_ = 0;
};

class I2cDevice : public fidl::WireServer<fuchsia_hardware_i2c::Device> {
 public:
  void Transfer(TransferRequestView request, TransferCompleter::Sync& completer) override {
    const fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction> transactions =
        request->transactions;

    if (std::any_of(transactions.cbegin(), transactions.cend(),
                    [](const fuchsia_hardware_i2c::wire::Transaction& t) {
                      return !t.has_data_transfer();
                    })) {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }

    fidl::Arena arena;
    fidl::ObjectView<fuchsia_hardware_i2c::wire::DeviceTransferResponse> response(arena);

    stop_.reserve(transactions.count());

    const size_t write_size = std::accumulate(
        transactions.cbegin(), transactions.cend(), 0,
        [](size_t a, const fuchsia_hardware_i2c::wire::Transaction& b) {
          return a +
                 (b.data_transfer().is_write_data() ? b.data_transfer().write_data().count() : 0);
        });
    tx_data_.resize(write_size);

    const size_t read_count = std::count_if(transactions.cbegin(), transactions.cend(),
                                            [](const fuchsia_hardware_i2c::wire::Transaction& t) {
                                              return t.data_transfer().is_read_size();
                                            });
    response->read_data = {arena, read_count};

    size_t tx_offset = 0;
    size_t rx_transaction = 0;
    size_t rx_offset = 0;
    auto stop_it = stop_.begin();
    for (const auto& transaction : transactions) {
      if (transaction.data_transfer().is_read_size()) {
        // If this is a write/read, pass back all of the expected read data, regardless of how much
        // the client requested. This allows the truncation behavior to be tested.
        const size_t read_size =
            read_count == 1 ? rx_data_.size() : transaction.data_transfer().read_size();

        // Copy the expected RX data to each read transaction.
        auto& read_data = response->read_data[rx_transaction++];
        read_data = {arena, read_size};

        if (rx_offset + read_data.count() > rx_data_.size()) {
          completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
          return;
        }

        memcpy(read_data.data(), &rx_data_[rx_offset], read_data.count());
        rx_offset += transaction.data_transfer().read_size();
      } else {
        // Serialize and store the write transaction.
        const auto& write_data = transaction.data_transfer().write_data();
        memcpy(&tx_data_[tx_offset], write_data.data(), write_data.count());
        tx_offset += write_data.count();
      }

      *stop_it++ = transaction.has_stop() ? transaction.stop() : false;
    }

    completer.Reply(::fit::ok(response.get()));
  }

  const std::vector<uint8_t>& tx_data() const { return tx_data_; }
  void set_rx_data(std::vector<uint8_t> rx_data) { rx_data_ = std::move(rx_data); }
  const std::vector<bool>& stop() const { return stop_; }

 private:
  std::vector<uint8_t> tx_data_;
  std::vector<uint8_t> rx_data_;
  std::vector<bool> stop_;
};

class I2cChannelTest : public zxtest::Test {
 public:
  I2cChannelTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() override { loop_.StartThread(); }

  void TearDown() override { loop_.Shutdown(); }

 protected:
  fidl::ClientEnd<fuchsia_hardware_i2c::Device> BindI2c(
      fidl::WireServer<fuchsia_hardware_i2c::Device>* server) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    EXPECT_TRUE(endpoints.is_ok());

    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), server);
    return std::move(endpoints->client);
  }

  async::Loop loop_;
};

TEST_F(I2cChannelTest, NoRetries) {
  FlakyI2cDevice i2c_dev;
  ddk::I2cChannel channel(BindI2c(&i2c_dev));
  // No retry, the first error is returned.
  uint8_t buffer[1] = {0x12};
  constexpr uint8_t kNumberOfRetries = 0;
  auto ret = channel.WriteSyncRetries(buffer, sizeof(buffer), kNumberOfRetries, zx::usec(1));
  EXPECT_EQ(ret.status, ZX_ERR_INTERNAL);
  EXPECT_EQ(ret.retries, 0);
}

TEST_F(I2cChannelTest, RetriesAllFail) {
  FlakyI2cDevice i2c_dev;
  ddk::I2cChannel channel(BindI2c(&i2c_dev));
  // 2 retries, corresponding error is returned. The first time Transact is called we get a
  // ZX_ERR_INTERNAL. Then the first retry gives us ZX_ERR_NOT_SUPPORTED and then the second
  // gives us ZX_ERR_NO_RESOURCES.
  constexpr uint8_t kNumberOfRetries = 2;
  uint8_t buffer[1] = {0x34};
  auto ret = channel.ReadSyncRetries(0x56, buffer, sizeof(buffer), kNumberOfRetries, zx::usec(1));
  EXPECT_EQ(ret.status, ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(ret.retries, 2);
}

TEST_F(I2cChannelTest, RetriesOk) {
  FlakyI2cDevice i2c_dev;
  ddk::I2cChannel channel(BindI2c(&i2c_dev));
  // 4 retries requested but no error, return ok.
  uint8_t tx_buffer[1] = {0x78};
  uint8_t rx_buffer[1] = {0x90};
  constexpr uint8_t kNumberOfRetries = 5;
  auto ret = channel.WriteReadSyncRetries(tx_buffer, sizeof(tx_buffer), rx_buffer,
                                          sizeof(rx_buffer), kNumberOfRetries, zx::usec(1));
  EXPECT_EQ(ret.status, ZX_OK);
  EXPECT_EQ(ret.retries, 4);
}

TEST_F(I2cChannelTest, FidlRead) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  ASSERT_TRUE(endpoints.is_ok());

  I2cDevice i2c_dev;
  auto binding = fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_dev);

  const std::array<uint8_t, 4> expected_rx_data{0x12, 0x34, 0xab, 0xcd};

  ddk::I2cChannel client(std::move(endpoints->client));
  i2c_dev.set_rx_data({expected_rx_data.data(), expected_rx_data.data() + expected_rx_data.size()});

  uint8_t buf[4];
  EXPECT_OK(client.ReadSync(0x89, buf, expected_rx_data.size()));
  ASSERT_EQ(i2c_dev.tx_data().size(), 1);
  EXPECT_EQ(i2c_dev.tx_data()[0], 0x89);
  EXPECT_BYTES_EQ(buf, expected_rx_data.data(), expected_rx_data.size());

  uint8_t buf1[5];
  // I2cChannel will copy as much data as it receives, even if it is less than the amount requested.
  EXPECT_OK(client.ReadSync(0x98, buf1, sizeof(buf1)));
  ASSERT_EQ(i2c_dev.tx_data().size(), 1);
  EXPECT_EQ(i2c_dev.tx_data()[0], 0x98);
  EXPECT_BYTES_EQ(buf1, expected_rx_data.data(), expected_rx_data.size());

  uint8_t buf2[3];
  // I2cChannel will copy no more than the amount requested.
  EXPECT_OK(client.ReadSync(0x18, buf2, sizeof(buf2)));
  ASSERT_EQ(i2c_dev.tx_data().size(), 1);
  EXPECT_EQ(i2c_dev.tx_data()[0], 0x18);
  EXPECT_BYTES_EQ(buf2, expected_rx_data.data(), sizeof(buf2));
}

TEST_F(I2cChannelTest, FidlWrite) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  ASSERT_TRUE(endpoints.is_ok());

  I2cDevice i2c_dev;
  auto binding = fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_dev);

  const std::array<uint8_t, 4> expected_tx_data{0x0f, 0x1e, 0x2d, 0x3c};

  ddk::I2cChannel client(std::move(endpoints->client));

  EXPECT_OK(client.WriteSync(expected_tx_data.data(), expected_tx_data.size()));
  ASSERT_EQ(i2c_dev.tx_data().size(), expected_tx_data.size());
  EXPECT_BYTES_EQ(i2c_dev.tx_data().data(), expected_tx_data.data(), expected_tx_data.size());
}

TEST_F(I2cChannelTest, FidlWriteRead) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  ASSERT_TRUE(endpoints.is_ok());

  I2cDevice i2c_dev;
  auto binding = fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_dev);

  const std::array<uint8_t, 4> expected_rx_data{0x12, 0x34, 0xab, 0xcd};
  const std::array<uint8_t, 4> expected_tx_data{0x0f, 0x1e, 0x2d, 0x3c};

  ddk::I2cChannel client(std::move(endpoints->client));
  i2c_dev.set_rx_data({expected_rx_data.data(), expected_rx_data.data() + expected_rx_data.size()});

  uint8_t buf[4];
  EXPECT_OK(client.WriteReadSync(expected_tx_data.data(), expected_tx_data.size(), buf,
                                 expected_rx_data.size()));
  ASSERT_EQ(i2c_dev.tx_data().size(), expected_tx_data.size());
  EXPECT_BYTES_EQ(i2c_dev.tx_data().data(), expected_tx_data.data(), expected_tx_data.size());
  EXPECT_BYTES_EQ(buf, expected_rx_data.data(), expected_rx_data.size());

  uint8_t buf1[5];
  EXPECT_OK(
      client.WriteReadSync(expected_tx_data.data(), expected_tx_data.size(), buf1, sizeof(buf1)));
  EXPECT_BYTES_EQ(i2c_dev.tx_data().data(), expected_tx_data.data(), expected_tx_data.size());
  EXPECT_BYTES_EQ(buf1, expected_rx_data.data(), expected_rx_data.size());

  uint8_t buf2[3];
  EXPECT_OK(
      client.WriteReadSync(expected_tx_data.data(), expected_tx_data.size(), buf2, sizeof(buf2)));
  EXPECT_BYTES_EQ(i2c_dev.tx_data().data(), expected_tx_data.data(), expected_tx_data.size());
  EXPECT_BYTES_EQ(buf2, expected_rx_data.data(), sizeof(buf2));

  EXPECT_OK(client.WriteReadSync(nullptr, 0, buf, expected_rx_data.size()));
  EXPECT_EQ(i2c_dev.tx_data().size(), 0);
  EXPECT_BYTES_EQ(buf, expected_rx_data.data(), expected_rx_data.size());
}

TEST_F(I2cChannelTest, GetFidlProtocolFromParent) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  ASSERT_TRUE(endpoints.is_ok());

  I2cDevice i2c_dev;
  auto binding = fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_dev);

  auto parent = MockDevice::FakeRootParent();

  parent->AddFidlProtocol(fidl::DiscoverableProtocolName<fuchsia_hardware_i2c::Device>,
                          [this, &i2c_dev](zx::channel channel) {
                            fidl::BindServer(
                                loop_.dispatcher(),
                                fidl::ServerEnd<fuchsia_hardware_i2c::Device>(std::move(channel)),
                                &i2c_dev);
                            return ZX_OK;
                          });

  ddk::I2cChannel client(parent.get());
  ASSERT_TRUE(client.is_valid());

  // Issue a simple call to make sure the connection is working.
  i2c_dev.set_rx_data({0xab});

  uint8_t rx;
  EXPECT_OK(client.ReadSync(0x89, &rx, 1));
  ASSERT_EQ(i2c_dev.tx_data().size(), 1);
  EXPECT_EQ(i2c_dev.tx_data()[0], 0x89);
  EXPECT_EQ(rx, 0xab);

  // Move the client and verify that the new one is functional.
  ddk::I2cChannel new_client = std::move(client);

  i2c_dev.set_rx_data({0x12});
  EXPECT_OK(new_client.ReadSync(0x34, &rx, 1));
  ASSERT_EQ(i2c_dev.tx_data().size(), 1);
  EXPECT_EQ(i2c_dev.tx_data()[0], 0x34);
  EXPECT_EQ(rx, 0x12);
}

TEST_F(I2cChannelTest, GetFidlProtocolFromFragment) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  ASSERT_TRUE(endpoints.is_ok());

  I2cDevice i2c_dev;
  auto binding = fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_dev);

  auto parent = MockDevice::FakeRootParent();

  parent->AddFidlProtocol(
      fidl::DiscoverableProtocolName<fuchsia_hardware_i2c::Device>,
      [this, &i2c_dev](zx::channel channel) {
        fidl::BindServer(loop_.dispatcher(),
                         fidl::ServerEnd<fuchsia_hardware_i2c::Device>(std::move(channel)),
                         &i2c_dev);
        return ZX_OK;
      },
      "fragment-name");

  ddk::I2cChannel client(parent.get(), "fragment-name");
  ASSERT_TRUE(client.is_valid());

  i2c_dev.set_rx_data({0x56});

  uint8_t rx;
  EXPECT_OK(client.ReadSync(0x78, &rx, 1));
  ASSERT_EQ(i2c_dev.tx_data().size(), 1);
  EXPECT_EQ(i2c_dev.tx_data()[0], 0x78);
  EXPECT_EQ(rx, 0x56);
}
