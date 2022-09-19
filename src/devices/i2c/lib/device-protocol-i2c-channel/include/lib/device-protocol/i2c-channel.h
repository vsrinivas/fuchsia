// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_
#define SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/ddk/driver.h>
#include <zircon/types.h>

#include <algorithm>
#include <optional>

namespace ddk {

class I2cChannel {
 public:
  struct StatusRetries {
    zx_status_t status;
    uint8_t retries;
  };

  I2cChannel() = default;

  I2cChannel(fidl::ClientEnd<fuchsia_hardware_i2c::Device> client)
      : fidl_client_(std::move(client)) {}

  I2cChannel(zx_device_t* parent) { ConnectFidl(parent, nullptr); }

  I2cChannel(zx_device_t* parent, const char* fragment_name) { ConnectFidl(parent, fragment_name); }

  I2cChannel(I2cChannel&& other) noexcept = default;
  I2cChannel& operator=(I2cChannel&& other) noexcept = default;

  I2cChannel(const I2cChannel& other) = delete;
  I2cChannel& operator=(const I2cChannel& other) = delete;

  ~I2cChannel() = default;

  // Performs typical i2c Read: writes device register address (1 byte) followed
  // by len reads into buf.
  zx_status_t ReadSync(uint8_t addr, uint8_t* buf, size_t len) {
    return WriteReadSync(&addr, 1, buf, len);
  }

  // Writes len bytes from buffer with no trailing read
  zx_status_t WriteSync(const uint8_t* buf, size_t len) {
    return WriteReadSync(buf, len, nullptr, 0);
  }

  // ReadSync with retries, returns status and retry attempts.
  StatusRetries ReadSyncRetries(uint8_t addr, uint8_t* buf, size_t len, uint8_t retries,
                                zx::duration delay) {
    return WriteReadSyncRetries(&addr, 1, buf, len, retries, delay);
  }

  // WriteSync with retries, returns status and retry attempts.
  StatusRetries WriteSyncRetries(const uint8_t* buf, size_t len, uint8_t retries,
                                 zx::duration delay) {
    return WriteReadSyncRetries(buf, len, nullptr, 0, retries, delay);
  }

  // WriteReadSync with retries, returns status and retry attempts.
  StatusRetries WriteReadSyncRetries(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf,
                                     size_t rx_len, uint8_t retries, zx::duration delay) {
    uint8_t attempt = 0;
    auto status = WriteReadSync(tx_buf, tx_len, rx_buf, rx_len);
    while (status != ZX_OK && attempt < retries) {
      zx::nanosleep(zx::deadline_after(delay));
      attempt++;
      status = WriteReadSync(tx_buf, tx_len, rx_buf, rx_len);
    }
    return {status, attempt};
  }

  zx_status_t WriteReadSync(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf, size_t rx_len) {
    if (tx_len > fuchsia_hardware_i2c::wire::kMaxTransferSize ||
        rx_len > fuchsia_hardware_i2c::wire::kMaxTransferSize) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    fidl::Arena arena;

    fidl::VectorView<uint8_t> write_data(arena, tx_len);
    if (tx_len) {
      memcpy(write_data.data(), tx_buf, tx_len);
    }

    auto write_transfer =
        fuchsia_hardware_i2c::wire::DataTransfer::WithWriteData(arena, write_data);
    auto read_transfer =
        fuchsia_hardware_i2c::wire::DataTransfer::WithReadSize(static_cast<uint32_t>(rx_len));

    fuchsia_hardware_i2c::wire::Transaction transactions[2];
    size_t index = 0;
    if (tx_len > 0) {
      transactions[index++] = fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
                                  .data_transfer(write_transfer)
                                  .Build();
    }
    if (rx_len > 0) {
      transactions[index++] = fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
                                  .data_transfer(read_transfer)
                                  .Build();
    }

    if (index == 0) {
      return ZX_ERR_INVALID_ARGS;
    }

    const auto reply = fidl_client_->Transfer(
        fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction>::FromExternal(transactions,
                                                                                index));
    if (!reply.ok()) {
      return reply.status();
    }
    if (reply->is_error()) {
      return reply->error_value();
    }

    if (rx_len > 0) {
      const auto& read_data = reply->value()->read_data;
      // Truncate the returned buffer to match the behavior of the Banjo version.
      if (read_data.count() != 1) {
        return ZX_ERR_IO;
      }

      memcpy(rx_buf, read_data[0].data(), std::min(rx_len, read_data[0].count()));
    }

    return ZX_OK;
  }

  fidl::WireResult<fuchsia_hardware_i2c::Device::Transfer> Transfer(
      fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction> transactions) {
    return fidl_client_->Transfer(transactions);
  }

  bool is_valid() const { return fidl_client_.is_valid(); }

 private:
  void ConnectFidl(zx_device_t* parent, const char* fragment_name) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    if (endpoints.is_error()) {
      return;
    }

    zx_status_t status;
    if (fragment_name == nullptr) {
      status = device_connect_fidl_protocol(
          parent, fidl::DiscoverableProtocolName<fuchsia_hardware_i2c::Device>,
          endpoints->server.TakeChannel().release());
    } else {
      status = device_connect_fragment_fidl_protocol(
          parent, fragment_name, fidl::DiscoverableProtocolName<fuchsia_hardware_i2c::Device>,
          endpoints->server.TakeChannel().release());
    }

    if (status != ZX_OK) {
      return;
    }

    fidl_client_ = fidl::WireSyncClient(std::move(endpoints->client));
  }

  fidl::WireSyncClient<fuchsia_hardware_i2c::Device> fidl_client_;
};

}  // namespace ddk

#endif  // SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_
