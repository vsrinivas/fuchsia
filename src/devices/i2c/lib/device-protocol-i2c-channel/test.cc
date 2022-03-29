// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <zircon/errors.h>

#include <vector>

#include <zxtest/zxtest.h>

class I2cDevice : public fake_i2c::FakeI2c, public fidl::WireServer<fuchsia_hardware_i2c::Device2> {
 public:
  void Transfer(TransferRequestView request, TransferCompleter::Sync& completer) override {
    if (request->segments_is_write.count() <= 0 || request->segments_is_write.count() > 2) {
      completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
      return;
    }

    const bool is_write = request->segments_is_write[0];
    const bool is_read = !request->segments_is_write[request->segments_is_write.count() - 1];

    if (request->segments_is_write.count() == 2 && (!is_write || !is_read)) {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }
    if (is_write && request->write_segments_data.count() != 1) {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }
    if (is_read && request->read_segments_length.count() != 1) {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
    }

    if (is_write &&
        request->write_segments_data[0].count() > fuchsia_hardware_i2c::wire::kMaxTransferSize) {
      completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    if (is_read && request->read_segments_length[0] != rx_data_.size()) {
      completer.ReplyError(ZX_ERR_IO);
      return;
    }

    if (is_write) {
      tx_data_ = std::vector<uint8_t>(request->write_segments_data[0].cbegin(),
                                      request->write_segments_data[0].cend());
    } else {
      tx_data_.clear();
    }

    fidl::Arena arena;

    fidl::ObjectView<fuchsia_hardware_i2c::wire::Device2TransferResponse> response(arena);
    if (is_read) {
      response->read_segments_data = fidl::VectorView<fidl::VectorView<uint8_t>>(arena, 1);
      response->read_segments_data[0] = fidl::VectorView<uint8_t>(arena, rx_data_.size());
      memcpy(response->read_segments_data[0].mutable_data(), rx_data_.data(), rx_data_.size());
    }

    completer.Reply(fuchsia_hardware_i2c::wire::Device2TransferResult::WithResponse(response));
  }

  const std::vector<uint8_t>& tx_data() const { return tx_data_; }
  void set_rx_data(std::vector<uint8_t> rx_data) { rx_data_ = std::move(rx_data); }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    count++;
    // Unique errors below to check for retries.
    switch (count) {
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
  size_t count = 0;
  std::vector<uint8_t> tx_data_;
  std::vector<uint8_t> rx_data_;
};

class I2cChannelTest : public zxtest::Test {
 public:
  I2cChannelTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override { loop_.StartThread(); }

 protected:
  async::Loop loop_;
};

TEST(I2cChannelTest, NoRetries) {
  I2cDevice i2c_dev;
  ddk::I2cChannel channel(i2c_dev.GetProto());
  // No retry, the first error is returned.
  uint8_t buffer[1] = {0x12};
  constexpr uint8_t kNumberOfRetries = 0;
  auto ret = channel.WriteSyncRetries(buffer, sizeof(buffer), kNumberOfRetries, zx::usec(1));
  EXPECT_EQ(ret.status, ZX_ERR_INTERNAL);
  EXPECT_EQ(ret.retries, 0);
}

TEST(I2cChannelTest, RetriesAllFail) {
  I2cDevice i2c_dev;
  ddk::I2cChannel channel(i2c_dev.GetProto());
  // 2 retries, corresponding error is returned. The first time Transact is called we get a
  // ZX_ERR_INTERNAL. Then the first retry gives us ZX_ERR_NOT_SUPPORTED and then the second
  // gives us ZX_ERR_NO_RESOURCES.
  constexpr uint8_t kNumberOfRetries = 2;
  uint8_t buffer[1] = {0x34};
  auto ret = channel.ReadSyncRetries(0x56, buffer, sizeof(buffer), kNumberOfRetries, zx::usec(1));
  EXPECT_EQ(ret.status, ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(ret.retries, 2);
}

TEST(I2cChannelTest, RetriesOk) {
  I2cDevice i2c_dev;
  ddk::I2cChannel channel(i2c_dev.GetProto());
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
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device2>();
  ASSERT_TRUE(endpoints.is_ok());

  I2cDevice i2c_dev;
  auto binding = fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_dev);

  const std::array<uint8_t, 4> expected_rx_data{0x12, 0x34, 0xab, 0xcd};

  ddk::I2cFidlChannel client(std::move(endpoints->client));
  i2c_dev.set_rx_data({expected_rx_data.data(), expected_rx_data.data() + expected_rx_data.size()});

  uint8_t buf[4];
  EXPECT_OK(client.ReadSync(0x89, buf, expected_rx_data.size()));
  ASSERT_EQ(i2c_dev.tx_data().size(), 1);
  EXPECT_EQ(i2c_dev.tx_data()[0], 0x89);
  EXPECT_BYTES_EQ(buf, expected_rx_data.data(), expected_rx_data.size());

  // Make sure the library propagates errors.
  EXPECT_NOT_OK(client.ReadSync(0x89, buf, 3));
}

TEST_F(I2cChannelTest, FidlWrite) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device2>();
  ASSERT_TRUE(endpoints.is_ok());

  I2cDevice i2c_dev;
  auto binding = fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_dev);

  const std::array<uint8_t, 4> expected_tx_data{0x0f, 0x1e, 0x2d, 0x3c};

  ddk::I2cFidlChannel client(std::move(endpoints->client));

  EXPECT_OK(client.WriteSync(expected_tx_data.data(), expected_tx_data.size()));
  ASSERT_EQ(i2c_dev.tx_data().size(), expected_tx_data.size());
  EXPECT_BYTES_EQ(i2c_dev.tx_data().data(), expected_tx_data.data(), expected_tx_data.size());
}

TEST_F(I2cChannelTest, FidlWriteRead) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device2>();
  ASSERT_TRUE(endpoints.is_ok());

  I2cDevice i2c_dev;
  auto binding = fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_dev);

  const std::array<uint8_t, 4> expected_rx_data{0x12, 0x34, 0xab, 0xcd};
  const std::array<uint8_t, 4> expected_tx_data{0x0f, 0x1e, 0x2d, 0x3c};

  ddk::I2cFidlChannel client(std::move(endpoints->client));
  i2c_dev.set_rx_data({expected_rx_data.data(), expected_rx_data.data() + expected_rx_data.size()});

  uint8_t buf[4];
  EXPECT_OK(client.WriteReadSync(expected_tx_data.data(), expected_tx_data.size(), buf,
                                 expected_rx_data.size()));
  ASSERT_EQ(i2c_dev.tx_data().size(), expected_tx_data.size());
  EXPECT_BYTES_EQ(i2c_dev.tx_data().data(), expected_tx_data.data(), expected_tx_data.size());
  EXPECT_BYTES_EQ(buf, expected_rx_data.data(), expected_rx_data.size());

  EXPECT_NOT_OK(client.WriteReadSync(expected_tx_data.data(), expected_tx_data.size(), buf, 3));

  EXPECT_OK(client.WriteReadSync(nullptr, 0, buf, expected_rx_data.size()));
  EXPECT_EQ(i2c_dev.tx_data().size(), 0);
  EXPECT_BYTES_EQ(buf, expected_rx_data.data(), expected_rx_data.size());
}
