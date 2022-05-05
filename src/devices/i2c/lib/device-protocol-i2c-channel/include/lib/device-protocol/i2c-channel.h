// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_
#define SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <lib/device-protocol/i2c.h>
#include <lib/sync/completion.h>
#include <zircon/types.h>

#include <optional>

namespace ddk {

// TODO(fxbug.dev/96293): Merge I2cFidlChannel back into I2cChannel and delete I2cChannelBase once
// all clients are using FIDL.
class I2cChannelBase {
 public:
  struct StatusRetries {
    zx_status_t status;
    uint8_t retries;
  };

  virtual ~I2cChannelBase() = default;

  // Performs typical i2c Read: writes device register address (1 byte) followed
  // by len reads into buf.
  zx_status_t ReadSync(uint8_t addr, uint8_t* buf, size_t len) {
    return WriteReadSync(&addr, 1, buf, len);
  }

  // Writes len bytes from buffer with no trailing read
  zx_status_t WriteSync(const uint8_t* buf, size_t len) {
    return WriteReadSync(buf, len, nullptr, 0);
  }

  virtual zx_status_t WriteReadSync(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf,
                                    size_t rx_len) = 0;

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
};

class I2cFidlChannel : public I2cChannelBase {
 public:
  explicit I2cFidlChannel(fidl::ClientEnd<fuchsia_hardware_i2c::Device> client_end)
      : fidl_client_(std::move(client_end)) {}

  I2cFidlChannel(I2cFidlChannel&& other) noexcept = default;
  I2cFidlChannel& operator=(I2cFidlChannel&& other) noexcept = default;

  ~I2cFidlChannel() override = default;

  fidl::WireResult<fuchsia_hardware_i2c::Device::Transfer> Transfer(
      fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction> transactions) {
    return fidl_client_->Transfer(transactions);
  }

  zx_status_t WriteReadSync(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf,
                            size_t rx_len) override {
    if (tx_len > fuchsia_hardware_i2c::wire::kMaxTransferSize ||
        rx_len > fuchsia_hardware_i2c::wire::kMaxTransferSize) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    fidl::Arena arena;

    fidl::VectorView<uint8_t> write_data(arena, tx_len);
    if (tx_len) {
      memcpy(write_data.mutable_data(), tx_buf, tx_len);
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
    if (reply->result.is_err()) {
      return reply->result.err();
    }

    if (rx_len > 0) {
      const auto& read_data = reply->result.response().read_data;
      if (read_data.count() != 1 || read_data[0].count() != rx_len) {
        return ZX_ERR_IO;
      }

      memcpy(rx_buf, read_data[0].data(), rx_len);
    }

    return ZX_OK;
  }

 private:
  fidl::WireSyncClient<fuchsia_hardware_i2c::Device> fidl_client_;
};

// TODO(fxbug.dev/96293): Remove Banjo support once all clients have been switched to FIDL.
class I2cChannel : public I2cChannelBase {
 public:
  I2cChannel() = default;

  I2cChannel(const i2c_protocol_t* proto) : banjo_client_(proto) {}

  I2cChannel(zx_device_t* parent) : banjo_client_(parent) { ConnectFidlIfNeeded(parent, nullptr); }

  I2cChannel(zx_device_t* parent, const char* fragment_name)
      : banjo_client_(parent, fragment_name) {
    ConnectFidlIfNeeded(parent, fragment_name);
  }

  I2cChannel(I2cChannel&& other) noexcept = default;
  I2cChannel& operator=(I2cChannel&& other) noexcept = default;

  I2cChannel(const I2cChannel& other) = delete;
  I2cChannel& operator=(const I2cChannel& other) = delete;

  ~I2cChannel() override = default;

  zx_status_t WriteReadSync(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf,
                            size_t rx_len) override {
    if (banjo_client_.is_valid()) {
      i2c_protocol_t proto;
      banjo_client_.GetProto(&proto);
      return i2c_write_read_sync(&proto, tx_buf, tx_len, rx_buf, rx_len);
    }
    if (fidl_client_.has_value()) {
      return fidl_client_->WriteReadSync(tx_buf, tx_len, rx_buf, rx_len);
    }
    ZX_ASSERT_MSG(false, "No Banjo or FIDL client is available");
  }

  void GetProto(i2c_protocol_t* proto) const {
    ZX_ASSERT_MSG(banjo_client_.is_valid(), "No Banjo client is available");
    banjo_client_.GetProto(proto);
  }

  bool is_valid() const { return banjo_client_.is_valid() || fidl_client_.has_value(); }

  void Transact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                void* cookie) const {
    // TODO(fxbug.dev/96293): Translate this into a (possibly async) FIDL call.
    ZX_ASSERT_MSG(!fidl_client_.has_value(), "Transact() is not implemented for FIDL clients");
    banjo_client_.Transact(op_list, op_count, callback, cookie);
  }

  zx_status_t GetMaxTransferSize(uint64_t* out_size) const {
    ZX_ASSERT_MSG(!fidl_client_.has_value(),
                  "GetMaxTransferSize() is not implemented for FIDL clients");
    return banjo_client_.GetMaxTransferSize(out_size);
  }

 private:
  void ConnectFidlIfNeeded(zx_device_t* parent, const char* fragment_name) {
    if (banjo_client_.is_valid()) {
      return;
    }

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

    fidl_client_.emplace(std::move(endpoints->client));
  }

  I2cProtocolClient banjo_client_;
  std::optional<I2cFidlChannel> fidl_client_;
};

}  // namespace ddk

#endif  // SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_
